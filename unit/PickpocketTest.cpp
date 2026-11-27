// Thornswood #6622. Pickpocketing is between people only (Patrick, 26 Nov
// 2026): a sneaking person's press on another person opens that person's
// pack in Skyrim's steal window, and the window takes only what the mark
// carries and does not wear. A press on an NPC, or by somebody who is not
// sneaking, opens nothing.
#include "TestUtils.hpp"
#include <catch2/catch_all.hpp>

#include "Messages.h"
#include "TestFormIds.h"

PartOne& GetPartOne();

using Catch::Matchers::ContainsSubstring;

namespace {
constexpr uint32_t kThief = 0xff000b10;
constexpr uint32_t kMark = 0xff000b11;
constexpr uint32_t kPickpocketNpc = 0xff000b12;
// WoodElfMalePreset10: an NPC_ record that is not the Player record.
constexpr uint32_t kNpcBase = 0x10abac;

bool OpenedFor(PartOne& p, uint32_t target)
{
  for (auto& m : p.Messages()) {
    if (m.userId == 0 && m.j["t"] == MsgType::OpenContainer &&
        m.j["target"] == target) {
      return true;
    }
  }
  return false;
}

struct People
{
  PartOne& p;
  MpActor& thief;
  MpActor& mark;
};

People TwoPeople()
{
  PartOne& p = GetPartOne();
  DoConnect(p, 0);
  p.CreateActor(kThief, { 0, 0, 0 }, 0, 0x3c, 1);
  p.SetUserActor(0, kThief);
  p.CreateActor(kMark, { 100, 0, 0 }, 0, 0x3c, 2);
  auto& thief = p.worldState.GetFormAt<MpActor>(kThief);
  auto& mark = p.worldState.GetFormAt<MpActor>(kMark);
  thief.RemoveAllItems();
  mark.RemoveAllItems();
  mark.AddItem(kGold, 10);
  p.Messages().clear();
  return { p, thief, mark };
}

void Done(People& w)
{
  DoDisconnect(w.p, 0);
  w.p.DestroyActor(kThief);
  w.p.DestroyActor(kMark);
}

void Sneak(MpActor& actor, bool sneaking)
{
  actor.SetAnimationVariableBool(AnimationVariableBool::kVariable_IsSneaking,
                                 sneaking);
}
}

TEST_CASE("Pickpocketing needs two people, a sneak and reach",
          "[Thornswood6622][espm]")
{
  auto w = TwoPeople();
  REQUIRE(w.thief.IsCreatedAsPlayer());
  REQUIRE(w.mark.IsCreatedAsPlayer());

  REQUIRE_FALSE(MpActor::MayPickpocket(w.thief, w.mark));
  Sneak(w.thief, true);
  REQUIRE(MpActor::MayPickpocket(w.thief, w.mark));
  REQUIRE_FALSE(MpActor::MayPickpocket(w.thief, w.thief));

  w.thief.SetPos({ 1000, 0, 0 });
  REQUIRE_FALSE(MpActor::MayPickpocket(w.thief, w.mark));
  w.thief.SetPos({ 0, 0, 0 });
  REQUIRE(MpActor::MayPickpocket(w.thief, w.mark));

  Done(w);
}

TEST_CASE("A sneaking person's press opens another person's pack",
          "[Thornswood6622][espm]")
{
  auto w = TwoPeople();

  // Not sneaking: the press opens nothing, and nothing can be taken.
  w.mark.Activate(w.thief);
  REQUIRE_FALSE(OpenedFor(w.p, kMark));
  REQUIRE_THROWS_WITH(w.mark.TakeItem(w.thief, { kGold, 1 }),
                      ContainsSubstring("doesn't occupy"));

  Sneak(w.thief, true);
  w.p.Messages().clear();
  w.mark.Activate(w.thief);
  REQUIRE(OpenedFor(w.p, kMark));

  // The mark's pack went to the thief before the window opened.
  bool packSent = false;
  for (auto& m : w.p.Messages()) {
    if (m.userId == 0 && m.j["t"] == MsgType::UpdateProperty &&
        m.j["propName"] == "inventory" && m.j["idx"] == w.mark.GetIdx()) {
      packSent = true;
    }
  }
  REQUIRE(packSent);

  w.mark.TakeItem(w.thief, { kGold, 3 });
  REQUIRE(w.thief.GetInventory().GetItemCount(kGold) == 3);
  REQUIRE(w.mark.GetInventory().GetItemCount(kGold) == 7);

  // The window closed: no more takes.
  w.mark.Activate(w.thief, false, true);
  REQUIRE_THROWS_WITH(w.mark.TakeItem(w.thief, { kGold, 1 }),
                      ContainsSubstring("doesn't occupy"));

  Done(w);
}

TEST_CASE("A pickpocket takes nothing the mark wears",
          "[Thornswood6622][espm]")
{
  auto w = TwoPeople();
  w.mark.SetInventory(Inventory().AddItems({ { kGold, 10 }, Worn(kSword) }));
  Equipment eq;
  eq.inv.AddItems({ Worn(kSword) });
  eq.numChanges = 1;
  w.mark.SetEquipment(eq);
  REQUIRE(w.mark.GetUnwornCount(kSword) == 0);
  REQUIRE(w.mark.GetUnwornCount(kGold) == 10);

  Sneak(w.thief, true);
  w.mark.Activate(w.thief);
  REQUIRE_THROWS_WITH(w.mark.TakeItem(w.thief, { kSword, 1 }),
                      ContainsSubstring("wears them"));
  REQUIRE(w.mark.GetInventory().GetItemCount(kSword) == 1);
  REQUIRE(w.thief.GetInventory().GetItemCount(kSword) == 0);

  Done(w);
}

TEST_CASE("A pickpocket who stands up or walks off takes nothing more",
          "[Thornswood6622][espm]")
{
  auto w = TwoPeople();
  Sneak(w.thief, true);
  w.mark.Activate(w.thief);
  REQUIRE(OpenedFor(w.p, kMark));

  Sneak(w.thief, false);
  REQUIRE_THROWS_WITH(w.mark.TakeItem(w.thief, { kGold, 1 }),
                      ContainsSubstring("can no longer pickpocket"));
  Sneak(w.thief, true);
  w.thief.SetPos({ 1000, 0, 0 });
  REQUIRE_THROWS_WITH(w.mark.TakeItem(w.thief, { kGold, 1 }),
                      ContainsSubstring("can no longer pickpocket"));
  REQUIRE(w.mark.GetInventory().GetItemCount(kGold) == 10);

  Done(w);
}

TEST_CASE("Nobody pickpockets an NPC", "[Thornswood6622][espm]")
{
  auto w = TwoPeople();
  w.p.worldState.AddForm(
    std::make_unique<MpActor>(
      LocationalData{ { 50, 0, 0 }, {}, FormDesc::Tamriel() },
      w.p.CreateFormCallbacks(), kNpcBase),
    kPickpocketNpc);
  auto& npc = w.p.worldState.GetFormAt<MpActor>(kPickpocketNpc);
  REQUIRE_FALSE(npc.IsCreatedAsPlayer());

  Sneak(w.thief, true);
  REQUIRE_FALSE(MpActor::MayPickpocket(w.thief, npc));
  w.p.Messages().clear();
  npc.Activate(w.thief);
  REQUIRE_FALSE(OpenedFor(w.p, kPickpocketNpc));

  w.p.worldState.DestroyForm<MpActor>(kPickpocketNpc);
  Done(w);
}
