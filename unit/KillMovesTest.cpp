#include "TestUtils.hpp"
#include <catch2/catch_all.hpp>
#include <chrono>
#include <set>

#include "ActionListener.h"
#include "HitMessage.h"
#include "KillMoveMessage.h"
#include "KillMoves.h"
#include "RawMessageData.h"
#include "libespm/Loader.h"
#include "TestFormIds.h"

PartOne& GetPartOne();
extern espm::Loader& GetEspmLoader();
using namespace std::chrono_literals;

// Thornswood #4507. The ids are the masters' own (Thornswood Ops
// issues/4507).
namespace {
constexpr uint32_t kBenthicLurkerKillMoves = 0x04030caa; // under FrontSide00
constexpr uint32_t kGiantKillMoveA = 0x9bb7e;            // under FrontSide
constexpr uint32_t kStabFromBehind = 0x0100081e;         // under BackSide
constexpr uint32_t kBackStab00 = 0x100f17;               // under BackSide00
constexpr uint32_t kFrontSideRoot = 0x24cd4;    // a root: no event of its own
constexpr uint32_t kBoarKillMoves = 0x04030c95; // groups its children
constexpr uint32_t kAttackRightRoot = 0x13211;

const auto kExtraWornTrue = [] {
  Inventory::ExtraData extra;
  extra.worn_ = true;
  return extra;
}();
}

TEST_CASE("KillMoves: a killmove fits only the attack whose roots it is under",
          "[KillMove][espm]")
{
  auto& br = GetEspmLoader().GetBrowser();
  espm::CompressedFieldsCache cache;

  REQUIRE(KillMoves::FitsAttack(br, cache, kBenthicLurkerKillMoves, false));
  REQUIRE(!KillMoves::FitsAttack(br, cache, kBenthicLurkerKillMoves, true));
  REQUIRE(KillMoves::FitsAttack(br, cache, kBackStab00, false));

  REQUIRE(KillMoves::FitsAttack(br, cache, kGiantKillMoveA, true));
  REQUIRE(!KillMoves::FitsAttack(br, cache, kGiantKillMoveA, false));
  REQUIRE(KillMoves::FitsAttack(br, cache, kStabFromBehind, true));

  // Nothing that is not a killmove the game plays
  for (bool power : { false, true }) {
    REQUIRE(!KillMoves::FitsAttack(br, cache, kFrontSideRoot, power));
    REQUIRE(!KillMoves::FitsAttack(br, cache, kBoarKillMoves, power));
    REQUIRE(!KillMoves::FitsAttack(br, cache, kAttackRightRoot, power));
    REQUIRE(!KillMoves::FitsAttack(br, cache, kIronDagger, power));
    REQUIRE(!KillMoves::FitsAttack(br, cache, 0, power));
    REQUIRE(!KillMoves::FitsAttack(br, cache, 0xfe123456, power));
  }

  // The first that fits, in the attacker's order
  REQUIRE(KillMoves::Pick(br, cache,
                          { kGiantKillMoveA, kFrontSideRoot,
                            kBenthicLurkerKillMoves, kBackStab00 },
                          false) == kBenthicLurkerKillMoves);
  REQUIRE(
    !KillMoves::Pick(br, cache, { kGiantKillMoveA, kIronDagger }, false));
  REQUIRE(!KillMoves::Pick(br, cache, {}, true));
}

namespace {
struct KillMoveScene
{
  static constexpr uint32_t kAttacker = 0xff004507;
  static constexpr uint32_t kTarget = 0xff004508;
  static constexpr uint32_t kBystander = 0xff004509;

  explicit KillMoveScene(PartOne& p_)
    : p(p_)
  {
    p.CreateActor(kAttacker, { 0, 0, 0 }, 0, 0x3c);
    p.CreateActor(kTarget, { 50, 0, 0 }, 0, 0x3c);
    p.CreateActor(kBystander, { 0, 50, 0 }, 0, 0x3c);
    DoConnect(p, 0);
    DoConnect(p, 1);
    p.SetUserActor(0, kAttacker);
    p.SetUserActor(1, kBystander);
    // An actor nobody plays joins the grid only when it moves or is
    // enabled; the target stands among the others like a placed NPC does.
    p.worldState.GetFormAt<MpActor>(kTarget).ForceSubscriptionsUpdate();

    auto& attacker = p.worldState.GetFormAt<MpActor>(kAttacker);
    attacker.AddItem(kIronDagger, 1);
    Equipment eq;
    eq.inv.entries.push_back(Inventory::Entry(kIronDagger, 1, kExtraWornTrue));
    attacker.SetEquipment(eq);
    attacker.SetLastHitTime(kTarget, std::chrono::steady_clock::now() - 10s);
  }

  ~KillMoveScene()
  {
    DoDisconnect(p, 0);
    DoDisconnect(p, 1);
    p.DestroyActor(kAttacker);
    p.DestroyActor(kTarget);
    p.DestroyActor(kBystander);
  }

  MpActor& Target() { return p.worldState.GetFormAt<MpActor>(kTarget); }

  void SetTargetHealth(float health)
  {
    ActorValues actorValues;
    actorValues.healthPercentage = health;
    actorValues.magickaPercentage = 1.f;
    actorValues.staminaPercentage = 1.f;
    Target().SetPercentages(actorValues);
  }

  void Hit(std::optional<std::vector<uint32_t>> killMoves,
           bool isPowerAttack = false)
  {
    RawMessageData rawMsgData;
    rawMsgData.userId = 0;
    HitMessage hitMsg;
    hitMsg.data.aggressor = 0x14;
    hitMsg.data.target = kTarget;
    hitMsg.data.source = kIronDagger;
    hitMsg.data.isPowerAttack = isPowerAttack;
    hitMsg.data.killMoves = std::move(killMoves);
    p.Messages().clear();
    p.GetActionListener().OnHit(rawMsgData, hitMsg);
  }

  std::vector<PartOne::Message> KillMoveMessages()
  {
    std::vector<PartOne::Message> res;
    for (auto& m : p.Messages()) {
      if (m.j.contains("t") &&
          m.j["t"] == static_cast<int>(MsgType::KillMove)) {
        res.push_back(m);
      }
    }
    return res;
  }

  PartOne& p;
};
}

TEST_CASE("KillMoves: a melee hit that kills sends its killmove to everybody "
          "who sees the target, before the zero and the death",
          "[KillMove][espm]")
{
  PartOne& p = GetPartOne();
  KillMoveScene scene(p);
  scene.SetTargetHealth(0.01f);

  scene.Hit(std::vector<uint32_t>{ kGiantKillMoveA, kBenthicLurkerKillMoves });

  REQUIRE(scene.Target().IsDead());
  const auto sent = scene.KillMoveMessages();
  std::set<Networking::UserId> users;
  for (auto& m : sent) {
    users.insert(m.userId);
    REQUIRE(m.reliable);
    REQUIRE(m.j["idle"] == kBenthicLurkerKillMoves);
    REQUIRE(
      m.j["idx"] ==
      p.worldState.GetFormAt<MpActor>(KillMoveScene::kAttacker).GetIdx());
    REQUIRE(m.j["targetIdx"] == scene.Target().GetIdx());
  }
  REQUIRE(users == std::set<Networking::UserId>{ 0, 1 });

  // The killmove goes out before the zero and the death
  size_t firstKillMove = p.Messages().size(), firstDeath = p.Messages().size();
  for (size_t i = 0; i < p.Messages().size(); ++i) {
    const auto& j = p.Messages()[i].j;
    if (!j.contains("t")) {
      continue;
    }
    if (j["t"] == static_cast<int>(MsgType::KillMove)) {
      firstKillMove = std::min(firstKillMove, i);
    }
    if (j["t"] == static_cast<int>(MsgType::DeathStateContainer) ||
        j["t"] == static_cast<int>(MsgType::ChangeValues)) {
      firstDeath = std::min(firstDeath, i);
    }
  }
  REQUIRE(firstKillMove < firstDeath);
}

TEST_CASE("KillMoves: no killmove without a death, without a fitting one, or "
          "without candidates",
          "[KillMove][espm]")
{
  PartOne& p = GetPartOne();

  SECTION("the hit does not kill")
  {
    KillMoveScene scene(p);
    scene.SetTargetHealth(1.f);
    scene.Hit(std::vector<uint32_t>{ kBenthicLurkerKillMoves });
    REQUIRE(!scene.Target().IsDead());
    REQUIRE(scene.KillMoveMessages().empty());
  }

  SECTION("none of the candidates fits a normal attack")
  {
    KillMoveScene scene(p);
    scene.SetTargetHealth(0.01f);
    scene.Hit(std::vector<uint32_t>{ kGiantKillMoveA, kFrontSideRoot });
    REQUIRE(scene.Target().IsDead());
    REQUIRE(scene.KillMoveMessages().empty());
  }

  SECTION("the attacker's game sent none, as an older client does")
  {
    KillMoveScene scene(p);
    scene.SetTargetHealth(0.01f);
    scene.Hit(std::nullopt);
    REQUIRE(scene.Target().IsDead());
    REQUIRE(scene.KillMoveMessages().empty());
  }

  SECTION("a killmove held for one hit is not played by a later death")
  {
    KillMoveScene scene(p);
    scene.SetTargetHealth(1.f);
    scene.Hit(std::vector<uint32_t>{ kBenthicLurkerKillMoves });
    p.Messages().clear();
    scene.Target().Kill(
      &p.worldState.GetFormAt<MpActor>(KillMoveScene::kAttacker));
    REQUIRE(scene.Target().IsDead());
    REQUIRE(scene.KillMoveMessages().empty());
  }
}
