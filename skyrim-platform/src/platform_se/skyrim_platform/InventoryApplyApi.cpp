#include "InventoryApplyApi.h"
#include "CallNativeApi.h"
#include "InventoryApplyScheduler.h"
#include "InventoryApplyPump.h"
#include "InventoryLoadEpoch.h"
#include "GameplayMenuUnpaused.h"
#include "PapyrusTESModPlatform.h"

#include <cmath>
#include <limits>
#include <set>

extern CallNativeApi::NativeCallRequirements g_nativeCallRequirements;

namespace {
using InventoryApply::Job;
using InventoryApply::State;
using InventoryApply::UnitResult;
InventoryApply::Scheduler scheduler;

// ==== inventory apply scope ====
struct Scope {
  uint32_t referenceId;
  RE::ObjectRefHandle handle;
  RE::TESObjectREFR* identity;
  uint64_t epoch;
  bool dead;
  std::atomic<bool> cancelled{false};
  mutable std::recursive_mutex mutex;
  std::shared_ptr<Job> previous, job;

  bool Current() const {
    if (cancelled.load(std::memory_order_acquire) || !InventoryLoadEpoch::IsCurrent(epoch) || !GameplayMenuUnpaused::HasLoadedWorld()) return false;
    auto held = handle.get();
    if (!held || held.get() != identity || RE::TESForm::LookupByID<RE::TESObjectREFR>(referenceId) != identity) return false;
    auto actor = identity->As<RE::Actor>();
    return !actor || actor->IsDead() == dead;
  }
  void Cancel() {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    cancelled.store(true, std::memory_order_release);
    if (job) job->Cancel();
  }
  bool Ready() const {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (!previous) return true;
    const auto state = previous->GetState();
    return state != State::Pending && state != State::Cancelling;
  }
  ~Scope() { Cancel(); }
};

// ==== end inventory apply scope ====

std::recursive_mutex scopesMutex;
std::unordered_map<uint32_t, std::weak_ptr<Scope>> scopes;
std::mutex environmentsMutex;
std::set<napi_env> environments;
InventoryApply::Pump pump(scheduler, [](std::function<void()> task) {
  auto queue = g_nativeCallRequirements.gameThrQ;
  if (!queue) throw std::runtime_error("Inventory game queue is unavailable");
  queue->AddTask([task](Viet::Void) { task(); });
});

uint32_t Uint32(Napi::Value value, bool positive = false) {
  if (!value.IsNumber()) throw std::invalid_argument("Invalid inventory integer");
  const double number = value.As<Napi::Number>().DoubleValue();
  if (!std::isfinite(number) || std::floor(number) != number || number < (positive ? 1 : 0) || number > UINT32_MAX)
    throw std::invalid_argument("Invalid inventory integer");
  return static_cast<uint32_t>(number);
}
int32_t Int32(Napi::Value value) {
  if (!value.IsNumber()) throw std::invalid_argument("Invalid inventory integer");
  const double number = value.As<Napi::Number>().DoubleValue();
  if (!std::isfinite(number) || std::floor(number) != number || number < INT32_MIN || number > INT32_MAX)
    throw std::invalid_argument("Invalid inventory integer");
  return static_cast<int32_t>(number);
}
float Float(Napi::Value value) {
  if (!value.IsNumber()) throw std::invalid_argument("Invalid inventory number");
  const double number = value.As<Napi::Number>().DoubleValue();
  if (!std::isfinite(number) || std::abs(number) > std::numeric_limits<float>::max())
    throw std::invalid_argument("Invalid inventory number");
  return static_cast<float>(number);
}
bool Bool(Napi::Value value) {
  if (!value.IsBoolean()) throw std::invalid_argument("Invalid inventory boolean");
  return value.As<Napi::Boolean>().Value();
}

struct Addition {
  uint32_t baseId, enchantmentId, poisonId, count, quantity;
  RE::TESForm* form;
  RE::EnchantmentItem* enchantment;
  RE::AlchemyItem* poison;
  float health, charge;
  int32_t maxCharge, soul, poisonCount;
  bool removeEnchantment, worn, wornLeft;
  std::string name;
};

std::vector<InventoryApply::Operation> Operations(Napi::Value value, const std::shared_ptr<Scope>& scope) {
  if (!value.IsArray()) throw std::invalid_argument("Invalid inventory additions");
  auto array = value.As<Napi::Array>();
  std::vector<InventoryApply::Operation> operations;
  // This iterates DESCRIPTORS, never their item counts. UINT32_MAX remains
  // one counter, not a vector of closures or a JS/native preparation loop.
  for (uint32_t i = 0; i < array.Length(); ++i) {
    auto entry = array.Get(i);
    if (!entry.IsObject() || entry.IsArray() || entry.IsNull()) throw std::invalid_argument("Invalid inventory addition");
    auto object = entry.As<Napi::Object>();
    auto addition = std::make_shared<Addition>();
    addition->baseId = Uint32(object.Get("baseId"), true);
    addition->count = Uint32(object.Get("count"), true);
    addition->quantity = Uint32(object.Get("quantityPerUnit"), true);
    if (addition->quantity > INT32_MAX) throw std::invalid_argument("Inventory native count exceeds int32");
    addition->health = Float(object.Get("health"));
    addition->enchantmentId = Uint32(object.Get("enchantmentId"));
    addition->maxCharge = Int32(object.Get("maxCharge"));
    addition->removeEnchantment = Bool(object.Get("removeEnchantmentOnUnequip"));
    addition->charge = Float(object.Get("chargePercent"));
    addition->soul = Int32(object.Get("soul"));
    addition->poisonId = Uint32(object.Get("poisonId"));
    addition->poisonCount = Int32(object.Get("poisonCount"));
    addition->worn = Bool(object.Get("worn"));
    addition->wornLeft = Bool(object.Get("wornLeft"));
    if (!object.Get("name").IsString()) throw std::invalid_argument("Invalid inventory name");
    addition->name = object.Get("name").As<Napi::String>().Utf8Value();
    if (addition->name.find('\0') != std::string::npos) throw std::invalid_argument("Invalid inventory name");
    addition->form = RE::TESForm::LookupByID(addition->baseId);
    addition->enchantment = addition->enchantmentId ? RE::TESForm::LookupByID<RE::EnchantmentItem>(addition->enchantmentId) : nullptr;
    addition->poison = addition->poisonId ? RE::TESForm::LookupByID<RE::AlchemyItem>(addition->poisonId) : nullptr;
    if (!addition->form || !addition->form->As<RE::TESBoundObject>() ||
        (addition->enchantmentId && !addition->enchantment) || (addition->poisonId && !addition->poison))
      throw std::invalid_argument("Inventory form is unavailable");
    const auto type = addition->form->GetFormType();
    const bool bulk = type == RE::FormType::Misc || type == RE::FormType::AlchemyItem ||
      type == RE::FormType::Ingredient || type == RE::FormType::Ammo;
    if (!bulk && addition->quantity != 1) throw std::invalid_argument("Inventory gear requires unit additions");
    std::weak_ptr<Scope> weak = scope;
    operations.push_back({addition->count, addition->quantity,
      addition->worn || addition->wornLeft ? 2u : 1u,
      [addition, weak](uint32_t quantity) {
        auto held = weak.lock();
        if (!held || !held->Current()) return UnitResult::Cancelled;
        auto ui = RE::UI::GetSingleton();
        if (!ui || ui->GameIsPaused()) return UnitResult::Deferred;
        // Runtime forms and reference IDs can be reused. Re-resolve every
        // unit and reject a replacement rather than dereferencing old forms.
        if (RE::TESForm::LookupByID(addition->baseId) != addition->form ||
            (addition->enchantmentId && RE::TESForm::LookupByID<RE::EnchantmentItem>(addition->enchantmentId) != addition->enchantment) ||
            (addition->poisonId && RE::TESForm::LookupByID<RE::AlchemyItem>(addition->poisonId) != addition->poison)) return UnitResult::Cancelled;
        const bool ran = TESModPlatform::AddItemExImmediate(
          g_nativeCallRequirements.vm, g_nativeCallRequirements.stackId, nullptr,
          held->identity, addition->form, static_cast<int32_t>(quantity), addition->health,
          addition->enchantment, addition->maxCharge, addition->removeEnchantment,
          addition->charge, FixedString(addition->name.c_str()), addition->soul,
          addition->poison, addition->poisonCount, addition->worn, addition->wornLeft,
          [held] { return held->Current(); });
        return ran ? UnitResult::Ran : (held->Current() ? UnitResult::Deferred : UnitResult::Cancelled);
      }});
    if ((addition->worn || addition->wornLeft) && scope->identity->As<RE::Actor>()) {
      operations.push_back({1, 1, 1, [weak](uint32_t) {
        auto held = weak.lock();
        if (!held || !held->Current()) return UnitResult::Cancelled;
        auto ui = RE::UI::GetSingleton();
        if (!ui || ui->GameIsPaused()) return UnitResult::Deferred;
        // Exactly CallNative.cpp's Actor.QueueNiNodeUpdate operation, inline
        // and budgeted here instead of creating another per-item queue task.
        held->identity->As<RE::Actor>()->DoReset3D(false);
        return UnitResult::Ran;
      }});
    }
  }
  return operations;
}

// ==== inventory apply state ====
const char* StateName(const std::shared_ptr<Scope>& scope) {
  std::lock_guard<std::recursive_mutex> lock(scope->mutex);
  if (!scope->Current()) scope->Cancel();
  if (!scope->job) return scope->Current() ? "preparing" : "cancelled";
  switch (scope->job->GetState()) {
    case State::Pending: return "pending";
    case State::Drained: return "drained";
    case State::Cancelling: return "cancelling";
    case State::Cancelled: return "cancelled";
    case State::Uncertain: return "uncertain";
  }
  return "uncertain";
}

// ==== end inventory apply state ====

// ==== inventory apply factory ====
std::shared_ptr<Scope> CreateScope(uint32_t id) {
  // Use the existing world/loading and game-functions context authorities.
  // The deferred preLoad JS event is too late to close native admission.
  if (!g_nativeCallRequirements.vm || !GameplayMenuUnpaused::HasLoadedWorld())
    throw std::logic_error("Inventory application is unavailable while loading or outside an update");
  auto reference = RE::TESForm::LookupByID<RE::TESObjectREFR>(id);
  if (!reference) throw std::invalid_argument("Inventory reference is unavailable");
  auto scope = std::make_shared<Scope>();
  scope->referenceId = id;
  scope->handle = reference->CreateRefHandle();
  scope->identity = reference;
  scope->epoch = InventoryLoadEpoch::Capture();
  auto actor = reference->As<RE::Actor>();
  scope->dead = actor && actor->IsDead();
  if (!scope->Current()) throw std::invalid_argument("Inventory reference is unavailable");
  // The scheduler owns the active barrier even when an intermediate JS
  // scope has been collected. Never recover quiescence only from weak scopes.
  scope->previous = scheduler.ReferenceBarrier(id);
  if (scope->previous) scope->previous->Cancel();
  {
    std::lock_guard<std::recursive_mutex> lock(scopesMutex);
    for (auto at = scopes.begin(); at != scopes.end();) {
      if (at->second.expired()) at = scopes.erase(at); else ++at;
    }
    if (auto old = scopes[id].lock()) {
      std::lock_guard<std::recursive_mutex> oldLock(old->mutex);
      old->Cancel();
    }
    scopes[id] = scope;
  }
  return scope;
}
// ==== end inventory apply factory ====

Napi::Value Create(const Napi::CallbackInfo& info) {
  if (info.Length() != 1) throw std::invalid_argument("Inventory scope requires one reference");
  const auto scope = CreateScope(Uint32(info[0], true));
  auto object = Napi::Object::New(info.Env());
  object.Set("isCurrent", Napi::Function::New(info.Env(), [scope](const Napi::CallbackInfo& call) {
    return Napi::Boolean::New(call.Env(), scope->Current());
  }));
  object.Set("isReady", Napi::Function::New(info.Env(), [scope](const Napi::CallbackInfo& call) {
    return Napi::Boolean::New(call.Env(), scope->Ready());
  }));
  object.Set("state", Napi::Function::New(info.Env(), [scope](const Napi::CallbackInfo& call) {
    return Napi::String::New(call.Env(), StateName(scope));
  }));
  object.Set("cancel", Napi::Function::New(info.Env(), [scope](const Napi::CallbackInfo&) { scope->Cancel(); }));
  object.Set("submit", Napi::Function::New(info.Env(), NapiHelper::WrapCppExceptions([scope](const Napi::CallbackInfo& call) -> Napi::Value {
    std::lock_guard<std::recursive_mutex> lock(scope->mutex);
    if (call.Length() != 1 || !g_nativeCallRequirements.vm || scope->job || !scope->Current() || !scope->Ready()) throw std::logic_error("Inventory scope is not ready");
    auto operations = Operations(call[0], scope);
    if (!scope->Current()) throw std::logic_error("Inventory scope changed during preparation");
    std::weak_ptr<Scope> weak = scope;
    scope->job = scheduler.Submit(scope->referenceId, reinterpret_cast<uintptr_t>(scope->identity), std::move(operations), [weak] {
      auto held = weak.lock(); return held && held->Current();
    });
    if (scheduler.HasPending()) pump.Queue();
    return call.Env().Undefined();
  })));
  return object;
}
}

void InventoryApplyApi::CancelReference(uint32_t referenceId) {
  std::lock_guard<std::recursive_mutex> lock(scopesMutex);
  auto found = scopes.find(referenceId);
  if (found != scopes.end()) {
    if (auto scope = found->second.lock()) scope->Cancel();
  }
}
void InventoryApplyApi::CancelAll() {
  {
    std::lock_guard<std::recursive_mutex> lock(scopesMutex);
    for (auto& entry : scopes) if (auto scope = entry.second.lock()) scope->Cancel();
    scopes.clear();
  }
  pump.Cancel();
}
void InventoryApplyApi::Register(Napi::Env env, Napi::Object& exports) {
  exports.Set("createInventoryApply", Napi::Function::New(env, NapiHelper::WrapCppExceptions(Create)));
  exports.Set("cancelInventoryApply", Napi::Function::New(env, NapiHelper::WrapCppExceptions([](const Napi::CallbackInfo& info) -> Napi::Value {
    if (info.Length() != 1) throw std::invalid_argument("Inventory cancellation requires one reference");
    CancelReference(Uint32(info[0], true));
    return info.Env().Undefined();
  })));
  // Native exports are registered again for plugin loads/reloads. Keep one
  // cleanup hook per actual environment rather than retaining a new closure
  // on every reload until process exit.
  std::lock_guard<std::mutex> lock(environmentsMutex);
  const auto nativeEnv = static_cast<napi_env>(env);
  if (environments.insert(nativeEnv).second) {
    try {
      env.AddCleanupHook([nativeEnv] {
        CancelAll();
        std::lock_guard<std::mutex> lock(environmentsMutex);
        environments.erase(nativeEnv);
      });
    } catch (...) {
      environments.erase(nativeEnv);
      throw;
    }
  }
}
