#include "KillMoveApi.h"
#include "KillMoveFit.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

/*
  THORNSWOOD PATCH (Thornswood #4507). Killmoves play on every machine near
  the kill.

  The killmoves the game starts from an attack hang under four roots in the
  idle tree, and GetGlobalValue(KillMove) == 1 is on those four and nowhere
  else (Thornswood Ops issues/4507, read from the masters): every machine
  sets KillMove to 0, so no game starts one by itself, and the server plays
  one on every machine near the target when a melee hit's death goes through
  (KillMoves.cpp in skymp5-server).

  Which killmoves fit a swing is judged here, by the game's own condition
  code (TESConditionItem::IsTrue), on the attacker's machine: the attacker's
  animation graph, whether it is staggered, the target's furniture pose and
  whether the target is the last enemy are known exactly here and nowhere
  else. Two conditions are the server's and are left out: ShouldAttackKill
  (whether this hit kills is the server's to say, after the gamemode's
  onDying) and GetGlobalValue(KillMove) (it is 0 here on purpose).

  The order is the tree's: depth first, siblings in their ANAM order (each
  idle names the one before it), which is the order the game takes them in.
  A node that names an event and also has children comes after its
  children: that is two killmoves in the masters (1HM_KillMoveSabreCat and
  its 00), and which of the two the game prefers there has not been
  measured.
*/
namespace {

constexpr uint32_t kKillMoveGlobal = 0x100f19; // GLOB KillMove

// The four roots, by the kind of attack (Skyrim.esm)
constexpr std::array<uint32_t, 2> kAttackRoots = { 0x100e8b, 0x100f16 };
constexpr std::array<uint32_t, 2> kPowerAttackRoots = { 0x24cd4, 0xe8458 };

bool IsServersCondition(const RE::TESConditionItem& item)
{
  using Id = RE::FUNCTION_DATA::FunctionID;
  const auto function = item.data.functionData.function.get();
  if (function == Id::kShouldAttackKill) {
    return true;
  }
  if (function == Id::kGetGlobalValue) {
    auto global = static_cast<RE::TESForm*>(item.data.functionData.params[0]);
    return global && global->GetFormID() == kKillMoveGlobal;
  }
  return false;
}

// The idle's own conditions, as the game combines them, the server's two
// held as true
bool ConditionsHold(const RE::TESIdleForm& idle, RE::Actor* attacker,
                    RE::Actor* target)
{
  RE::ConditionCheckParams params(attacker, target);
  return KillMoveFit::GroupsHold(
    idle.conditions.head,
    [](const RE::TESConditionItem* item) { return item->next; },
    [](const RE::TESConditionItem* item) { return item->data.flags.isOR; },
    [&](const RE::TESConditionItem* item) {
      return IsServersCondition(*item) || item->IsTrue(params);
    });
}

bool IsRoot(const RE::TESIdleForm* idle, const std::array<uint32_t, 2>& roots)
{
  return idle &&
    std::find(roots.begin(), roots.end(), idle->GetFormID()) != roots.end();
}

}

Napi::Value KillMoveApi::GetFittingKillMoves(const Napi::CallbackInfo& info)
{
  auto env = info.Env();
  const uint32_t attackerId =
    NapiHelper::ExtractUInt32(info[0], "attackerFormId");
  const uint32_t targetId = NapiHelper::ExtractUInt32(info[1], "targetFormId");
  const bool isPowerAttack =
    NapiHelper::ExtractBoolean(info[2], "isPowerAttack");
  const auto& roots = isPowerAttack ? kPowerAttackRoots : kAttackRoots;

  auto result = Napi::Array::New(env);

  auto attacker = RE::TESForm::LookupByID<RE::Actor>(attackerId);
  auto target = RE::TESForm::LookupByID<RE::Actor>(targetId);
  auto dataHandler = RE::TESDataHandler::GetSingleton();
  if (!attacker || !target || !dataHandler) {
    return result;
  }

  auto parent = [](const RE::TESIdleForm* idle) { return idle->parentIdle; };
  auto previous = [](const RE::TESIdleForm* idle) { return idle->prevIdle; };
  std::unordered_map<const RE::TESIdleForm*, bool> holds;

  struct Found
  {
    std::vector<int> key;
    uint32_t formId = 0;
  };
  std::vector<Found> found;
  for (auto idle : dataHandler->GetFormArray<RE::TESIdleForm>()) {
    if (!idle || idle->animEventName.empty()) {
      continue;
    }
    const bool fits = KillMoveFit::ChainHolds<RE::TESIdleForm>(
      idle, parent, [&](const RE::TESIdleForm* n) { return IsRoot(n, roots); },
      [&](const RE::TESIdleForm* n) {
        return ConditionsHold(*n, attacker, target);
      },
      holds);
    if (fits) {
      found.push_back({ KillMoveFit::TreeOrderKey(idle, parent, previous),
                        idle->GetFormID() });
    }
  }
  std::sort(found.begin(), found.end(),
            [](const Found& a, const Found& b) { return a.key < b.key; });

  for (size_t i = 0; i < found.size(); ++i) {
    result.Set(static_cast<uint32_t>(i),
               Napi::Number::New(env, static_cast<double>(found[i].formId)));
  }
  return result;
}

void KillMoveApi::Register(Napi::Env env, Napi::Object& exports)
{
  exports.Set("getFittingKillMoves",
              Napi::Function::New(
                env, NapiHelper::WrapCppExceptions(GetFittingKillMoves)));
}
