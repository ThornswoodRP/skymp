#include "TestUtils.hpp"
#include "UpdateEquipmentMessage.h"
#include "TestFormIds.h"

// THORNSWOOD (Thornswood #6717). A granted item stayed out of the game until
// a later login. The owner's client dresses its character at login from the
// equipment record, finding one unit of the pack for each worn entry
// (remoteServer.ts, applyPcInv), and while that has not worked it keeps
// every pack the server sends out of the game. A record that names a worn
// piece the pack no longer holds can never work, so every pack write of that
// session, a /give included, stayed on the server. The engine wrote packs
// without looking at the record, so a worn piece the server took out of a
// pack stayed on the record unless the owner's game happened to report
// taking it off. Now a write that takes from an actor's pack drops such a
// piece from the record in the same edit and tells everybody who can see the
// actor.
namespace {
constexpr uint32_t kCuirass = 0x13961; // Iron Armor, Skyrim.esm
constexpr uint32_t kBoots = 0x1394d;   // Iron Boots
constexpr uint32_t kGiven = 0x13965;   // Iron Helmet
constexpr uint32_t kOwner = 0xff000abc;
constexpr uint32_t kViewer = 0xffabcabc;

Inventory::Entry WornLeft(uint32_t baseId)
{
  Inventory::ExtraData extra;
  extra.wornLeft = true;
  return Inventory::Entry(baseId, 1, extra);
}

// The owner's record, as sent to userId.
size_t RecordsSentTo(PartOne& p, Networking::UserId userId)
{
  const auto idx = p.worldState.GetFormAt<MpActor>(kOwner).GetIdx();
  size_t n = 0;
  for (auto& m : p.Messages()) {
    auto eq = dynamic_cast<UpdateEquipmentMessage*>(m.message.get());
    if (m.userId == userId && eq && eq->idx == idx) {
      ++n;
    }
  }
  return n;
}

MpActor& DressedOwnerSeenByViewer(PartOne& p)
{
  DoConnect(p, 0);
  p.CreateActor(kOwner, { 1.f, 2.f, 3.f }, 180.f, 0x3c);
  p.SetUserActor(0, kOwner);
  auto& owner = p.worldState.GetFormAt<MpActor>(kOwner);
  GiveStoredAppearance(owner);

  DoConnect(p, 1);
  p.CreateActor(kViewer, { 11.f, 22.f, 33.f }, 180.f, 0x3c);
  p.SetUserActor(1, kViewer);
  GiveStoredAppearance(p.worldState.GetFormAt<MpActor>(kViewer));

  owner.SetInventory(
    Inventory().AddItems({ Worn(kCuirass), Worn(kBoots), Worn(kSword) }));
  Equipment eq;
  eq.inv.AddItems({ Worn(kCuirass), Worn(kBoots), Worn(kSword) });
  eq.numChanges = 4;
  owner.SetEquipment(eq);
  p.Messages().clear();
  return owner;
}

std::vector<uint32_t> WornIds(const MpActor& actor)
{
  std::vector<uint32_t> ids;
  for (auto& entry : actor.GetEquipment().inv.entries) {
    if (entry.GetWorn() != Inventory::Worn::None) {
      ids.push_back(entry.baseId);
    }
  }
  return ids;
}
}

TEST_CASE("Thornswood #6717: the record keeps a worn piece only while the "
          "pack has a unit of it",
          "[Actor][6717]")
{
  Inventory::ExtraData spare;
  spare.name = "Iron Sword (85%)";
  Equipment eq;
  eq.inv.entries.push_back(Worn(kCuirass));
  eq.inv.entries.push_back(Worn(kSword));
  eq.inv.entries.push_back(WornLeft(kSword));
  eq.inv.entries.push_back(Inventory::Entry(kBoots, 1, spare));
  eq.numChanges = 9;

  SECTION("everything is in the pack: nothing changes")
  {
    Inventory pack;
    pack.AddItems(
      { Inventory::Entry(kCuirass, 1), Inventory::Entry(kSword, 2) });
    Equipment same = eq;
    REQUIRE_FALSE(MpActor::DropWornPiecesThePackLacks(same, pack));
    REQUIRE(same == eq);
  }

  SECTION("a piece under another name is the same piece")
  {
    Inventory::ExtraData renamed;
    renamed.name = "Iron Armor (40%)";
    Inventory pack;
    pack.AddItems(
      { Inventory::Entry(kCuirass, 1, renamed), Inventory::Entry(kSword, 2) });
    Equipment same = eq;
    REQUIRE_FALSE(MpActor::DropWornPiecesThePackLacks(same, pack));
    REQUIRE(same == eq);
  }

  SECTION("a piece the pack lost is dropped, and the change is counted")
  {
    Inventory pack;
    pack.AddItems({ Inventory::Entry(kSword, 2) });
    REQUIRE(MpActor::DropWornPiecesThePackLacks(eq, pack));
    REQUIRE(eq.inv.entries.size() == 3);
    REQUIRE(eq.inv.entries[0].baseId == kSword);
    REQUIRE(eq.inv.entries[1].baseId == kSword);
    REQUIRE(eq.inv.entries[1].GetWorn() == Inventory::Worn::Left);
    REQUIRE(eq.numChanges == 10);
  }

  SECTION("each worn entry takes its own unit: one sword is one hand")
  {
    Inventory pack;
    pack.AddItems(
      { Inventory::Entry(kCuirass, 1), Inventory::Entry(kSword, 1) });
    REQUIRE(MpActor::DropWornPiecesThePackLacks(eq, pack));
    REQUIRE(eq.inv.entries.size() == 3);
    REQUIRE(eq.inv.entries[1].baseId == kSword);
    REQUIRE(eq.inv.entries[1].GetWorn() == Inventory::Worn::Right);
    // What is not worn is not the record's to keep in step.
    REQUIRE(eq.inv.entries[2].baseId == kBoots);
  }
}

TEST_CASE("Thornswood #6717: a pack written without a worn piece takes it "
          "off the record and says so",
          "[PartOne][6717]")
{
  PartOne p;
  auto& owner = DressedOwnerSeenByViewer(p);

  SECTION("a pack that still holds everything leaves the record alone")
  {
    const Equipment before = owner.GetEquipment();
    owner.SetInventory(
      Inventory().AddItems({ Worn(kCuirass), Worn(kBoots), Worn(kSword),
                             Inventory::Entry(kGiven, 1) }));
    REQUIRE(owner.GetEquipment() == before);
    REQUIRE(RecordsSentTo(p, 1) == 0);
  }

  SECTION("mp.set of the inventory, as a trade writes it")
  {
    owner.SetInventory(Inventory().AddItems({ Worn(kBoots), Worn(kSword) }));
    REQUIRE(WornIds(owner) == std::vector<uint32_t>{ kBoots, kSword });
    REQUIRE(owner.GetEquipment().numChanges == 5);
    REQUIRE(owner.GetChangeForm().equipment == owner.GetEquipment());
    REQUIRE(RecordsSentTo(p, 1) == 1);
  }

  SECTION("a removal into another container")
  {
    auto& viewer = p.worldState.GetFormAt<MpActor>(kViewer);
    owner.RemoveItems({ Worn(kCuirass) }, &viewer);
    REQUIRE(WornIds(owner) == std::vector<uint32_t>{ kBoots, kSword });
    REQUIRE(viewer.GetInventory().GetItemCount(kCuirass) == 1);
    REQUIRE(RecordsSentTo(p, 1) == 1);
  }

  SECTION("a record already naming a lost piece is put right by the next "
          "pack write, a /give included")
  {
    Equipment stale = owner.GetEquipment();
    stale.inv.entries.push_back(Worn(0x13989)); // Iron Gauntlets, not carried
    owner.SetEquipment(stale);
    auto pack = owner.GetInventory();
    pack.AddItems({ Inventory::Entry(kGiven, 1) });
    owner.SetInventory(pack);
    REQUIRE(WornIds(owner) ==
            std::vector<uint32_t>{ kCuirass, kBoots, kSword });
    REQUIRE(owner.GetInventory().GetItemCount(kGiven) == 1);
  }
}

PartOne& GetPartOne();

TEST_CASE("Thornswood #6717: an NPC's leveled outfit is drawn once for its "
          "pack and its record",
          "[NpcBodyLoot][6717][espm]")
{
  PartOne& p = GetPartOne();
  p.worldState.npcEnabled = true;
  p.worldState.npcSettings["Skyrim.esm"].spawnInInterior = true;
  // MS13BanditCampfire01: its outfit comes from leveled lists.
  auto& bandit = p.worldState.GetFormAt<MpActor>(0x39fe4);
  REQUIRE_FALSE(bandit.GetEquipment().inv.IsEmpty());
  // Each draw is random, so look at several fresh ones.
  for (int i = 0; i < 20; ++i) {
    bandit.SetInventory(Inventory());
    bandit.RelootContainer();
    Equipment record = bandit.GetEquipment();
    REQUIRE_FALSE(
      MpActor::DropWornPiecesThePackLacks(record, bandit.GetInventory()));
  }
}
