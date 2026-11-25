#include <cassert>
#include <cstring>
#include <iostream>
#include <map>
#include "../../../skyrim-platform/src/platform_se/skyrim_platform/InventoryApplyScheduler.h"
#include "../../../skyrim-platform/src/platform_se/skyrim_platform/InventoryLoadEpoch.h"
#include "../../../skyrim-platform/src/platform_lib/GameplayMenuPolicy.h"

namespace GameplayMenuUnpaused {
GameplayMenuPolicy::World world;
bool HasLoadedWorld() { return world.HasLoadedWorld(); }
}
struct {void* vm = reinterpret_cast<void*>(1);} g_nativeCallRequirements;

namespace RE {
struct TESObjectREFR;
struct Slot { TESObjectREFR* reference; };
struct Resolved {
  TESObjectREFR* reference;
  TESObjectREFR* get() const { return reference; }
  explicit operator bool() const { return reference != nullptr; }
};
struct ObjectRefHandle {
  std::shared_ptr<Slot> slot;
  Resolved get() const { return {slot ? slot->reference : nullptr}; }
};
struct TESForm {
  static inline std::map<uint32_t, TESForm*> forms;
  virtual ~TESForm() = default;
  template<class T> T* As() { return dynamic_cast<T*>(this); }
  template<class T> static T* LookupByID(uint32_t id) { return dynamic_cast<T*>(forms[id]); }
};
struct TESObjectREFR : TESForm {
  std::shared_ptr<Slot> slot = std::make_shared<Slot>(Slot{this});
  ObjectRefHandle CreateRefHandle() { return {slot}; }
};
struct Actor : TESObjectREFR {
  bool dead = false;
  bool IsDead() const { return dead; }
};
}
using InventoryApply::Job;
using InventoryApply::State;
using InventoryApply::UnitResult;
InventoryApply::Scheduler scheduler([] { return uint64_t{0}; });
#include "InventoryScopeUnderTest.inc"
std::recursive_mutex scopesMutex;
std::unordered_map<uint32_t, std::weak_ptr<Scope>> scopes;
#include "InventoryStateUnderTest.inc"
#include "InventoryFactoryUnderTest.inc"

int main() {
  RE::Actor actor;
  RE::TESForm::forms[0x14] = &actor;
  try {CreateScope(0x14); assert(false);} catch(const std::logic_error&) {}
  GameplayMenuUnpaused::world.Loaded(true);
  auto a = CreateScope(0x14);
  assert(a->Current() && a->Ready() && !std::strcmp(StateName(a), "preparing"));
  int mutations = 0, preparations = 0;
  std::shared_ptr<Scope> c;
  a->job = scheduler.Submit(0x14, reinterpret_cast<uintptr_t>(&actor), {{1, 1, 1, [&](uint32_t) {
    ++mutations;
    auto b = CreateScope(0x14);
    assert(!a->Current() && !b->Ready());
    assert(!std::strcmp(StateName(a), "cancelling"));
    std::weak_ptr<Scope> collected = b;
    b.reset();
    assert(collected.expired());
    c = CreateScope(0x14);
    assert(!c->Ready());
    if (c->Ready()) ++preparations;
    return UnitResult::Ran;
  }}}, [a] {return a->Current();});
  scheduler.Tick();
  assert(mutations == 1 && preparations == 0);
  assert(!std::strcmp(StateName(a), "cancelled") && c->Ready());
  if (c->Ready()) ++preparations;
  assert(preparations == 1);
  auto replacement = CreateScope(0x14);
  assert(!c->Current() && replacement->Current());
  actor.dead = true;
  assert(!replacement->Current() && !std::strcmp(StateName(replacement), "cancelled"));
  auto corpse = CreateScope(0x14);
  assert(corpse->Current()); // Ordinary corpse inventory remains supported.
  GameplayMenuUnpaused::world.Loading();
  InventoryLoadEpoch::Advance();
  assert(!corpse->Current());
  // The old player pointer still exists and the deferred JS preLoad event
  // has not run. A queued old JS apply cannot capture the NEW epoch here.
  try {CreateScope(0x14); assert(false);} catch(const std::logic_error&) {}
  GameplayMenuUnpaused::world.Loaded(false);
  try {CreateScope(0x14); assert(false);} catch(const std::logic_error&) {}
  GameplayMenuUnpaused::world.Loaded(true);
  g_nativeCallRequirements.vm = nullptr;
  try {CreateScope(0x14); assert(false);} catch(const std::logic_error&) {}
  g_nativeCallRequirements.vm = reinterpret_cast<void*>(1);
  auto current = CreateScope(0x14);
  actor.slot->reference = nullptr;
  assert(!current->Current());
  std::cout << "PASS actual native scope cancellation, running state, collected replacement barrier, death, corpse and load identity\n";
}
