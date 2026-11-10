// THORNSWOOD PATCH (Thornswood #4513). A made enchantment is kept as what is
// in it, and a character holding a brewed potion and a self-enchanted piece
// comes back from a save with both, the same.
//
// The game makes an enchantment put on a piece at an arcane enchanter as a
// runtime form (above 0xff000000) only that session has, the same as a
// brewed bottle (#1649). The craft used to reach the server as the plain
// piece, so the piece went into the pack plain, and an entry could only name
// an enchantment by a form id the next session does not have. Now the craft
// carries what the client read off the piece, CraftService::MadeEnchantment
// checks it, and the entry holds Inventory::MadeEnchantment under
// Inventory::MadeEnchantmentId.
#include "TestUtils.hpp"
#include <catch2/catch_all.hpp>

#include "ActionListener.h"
#include "CraftItemMessage.h"
#include "CraftService.h"
#include "MpChangeForms.h"
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>

PartOne& GetPartOne();

namespace {
constexpr uint32_t kForge = 0x1ad6e;
constexpr uint32_t kIronDagger = 0x1397e;
constexpr uint32_t kIronHelmet = 0x12e4d;
constexpr uint32_t kPettySoulGem = 0x2e4e2;
constexpr uint32_t kWheat = 0x4b0ba;
constexpr uint32_t kRestoreHealth = 0x3eb15;
constexpr uint32_t kFortifyHealth = 0x3eaf3;

Inventory::MadeEnchantment OnDagger(float magnitude = 10.f)
{
  Inventory::MadeEnchantment enchantment;
  enchantment.weapon = true;
  enchantment.effects.push_back({ kRestoreHealth, magnitude, 0, 1 });
  return enchantment;
}

Inventory::MadeEnchantment OnHelmet()
{
  Inventory::MadeEnchantment enchantment;
  enchantment.weapon = false;
  enchantment.effects.push_back({ kFortifyHealth, 25.f, 0, 0 });
  return enchantment;
}

Inventory::Made Brewed()
{
  Inventory::Made made;
  made.name = "Potion of Restore Health";
  made.effects.push_back({ kRestoreHealth, 12.5f, 0, 0 });
  made.effects.push_back({ kFortifyHealth, 8.f, 0, 60 });
  return made;
}

CraftService::Claimed Claim(std::optional<Inventory::MadeEnchantment> e,
                            std::optional<float> maxCharge)
{
  CraftService::Claimed claimed;
  claimed.enchantment = std::move(e);
  claimed.maxCharge = maxCharge;
  return claimed;
}

CraftItemMessage Enchant(uint32_t piece,
                         std::optional<Inventory::MadeEnchantment> e,
                         std::optional<float> maxCharge)
{
  CraftItemMessage msg;
  msg.data.craftInputObjects =
    Inventory().AddItem(piece, 1).AddItem(kPettySoulGem, 1);
  msg.data.workbench = kForge;
  // An enchanted piece keeps its own record, so what arrives is the piece.
  msg.data.resultObjectId = piece;
  msg.data.resultEnchantment = std::move(e);
  msg.data.resultMaxCharge = maxCharge;
  return msg;
}

MpActor& MakeEnchanter(PartOne& p)
{
  auto& refr = p.worldState.GetFormAt<MpObjectReference>(kForge);
  DoConnect(p, 0);
  p.CreateActor(0xff000000, refr.GetPos(), 0,
                refr.GetCellOrWorld().ToFormId(p.worldState.espmFiles));
  p.SetUserActor(0, 0xff000000);
  return p.worldState.GetFormAt<MpActor>(0xff000000);
}

const Inventory::Entry* Enchanted(const Inventory& inv, uint32_t baseId)
{
  for (auto& entry : inv.entries) {
    if (entry.baseId == baseId && entry.madeEnchantment) {
      return &entry;
    }
  }
  return nullptr;
}

// The change form as a save writes it and a load reads it back.
MpChangeForm ThroughSave(const MpChangeForm& changeForm)
{
  static simdjson::dom::parser parser;
  const std::string dump = MpChangeForm::ToJson(changeForm).dump();
  simdjson::dom::element element = parser.parse(dump).value();
  return MpChangeForm::JsonToChangeForm(element);
}
}

TEST_CASE("A made enchantment has one id for its contents, never a bottle's "
          "(Thornswood #4513)",
          "[MadeEnchantment]")
{
  const uint32_t id = Inventory::MadeEnchantmentId(OnDagger());
  REQUIRE(id >= Inventory::kMadeIdFirst);
  REQUIRE(Inventory::MadeEnchantmentId(OnDagger()) == id);
  REQUIRE(Inventory::MadeEnchantmentId(OnDagger(11.f)) != id);

  // The same effects on armour are another enchantment.
  auto onArmour = OnDagger();
  onArmour.weapon = false;
  REQUIRE(Inventory::MadeEnchantmentId(onArmour) != id);
  REQUIRE(Inventory::Canonical(OnDagger()) ==
          "weapon-enchantment|3eb15:10.00:0:1");
  REQUIRE(Inventory::Canonical(onArmour) ==
          "armour-enchantment|3eb15:10.00:0:1");

  auto noisy = OnDagger(10.0000001f);
  Inventory::Round(noisy);
  REQUIRE(noisy == OnDagger());
}

TEST_CASE("A character holding a brewed potion and a self-enchanted piece "
          "comes back from a save with both, the same (Thornswood #4513)",
          "[MadeEnchantment]")
{
  Inventory::ExtraData bottle;
  bottle.made = Brewed();
  const uint32_t bottleId = Inventory::MadeBaseId(*bottle.made);

  Inventory::ExtraData dagger;
  dagger.madeEnchantment = OnDagger();
  dagger.enchantmentId = Inventory::MadeEnchantmentId(OnDagger());
  dagger.maxCharge = 250.f;

  Inventory::ExtraData helmet;
  helmet.madeEnchantment = OnHelmet();
  helmet.enchantmentId = Inventory::MadeEnchantmentId(OnHelmet());
  helmet.worn_ = true;

  MpChangeForm character;
  character.recType = MpChangeForm::ACHR;
  character.inv.AddItems({ Inventory::Entry(bottleId, 2, bottle),
                           Inventory::Entry(kIronDagger, 1, dagger),
                           Inventory::Entry(kIronHelmet, 1, helmet) });

  const auto back = ThroughSave(character);
  REQUIRE(back.inv == character.inv);

  REQUIRE(back.inv.GetItemCount(bottleId) == 2);
  REQUIRE(back.inv.entries[0].made == Brewed());

  const auto* piece = Enchanted(back.inv, kIronDagger);
  REQUIRE(piece);
  REQUIRE(*piece->madeEnchantment == OnDagger());
  REQUIRE(piece->enchantmentId ==
          Inventory::MadeEnchantmentId(*piece->madeEnchantment));
  REQUIRE(piece->maxCharge == 250.f);

  const auto* worn = Enchanted(back.inv, kIronHelmet);
  REQUIRE(worn);
  REQUIRE(*worn->madeEnchantment == OnHelmet());
  REQUIRE(worn->enchantmentId ==
          Inventory::MadeEnchantmentId(*worn->madeEnchantment));

  // A plain dagger and the enchanted one do not stack.
  Inventory inv = back.inv;
  inv.AddItem(kIronDagger, 1);
  REQUIRE(inv.GetItemCount(kIronDagger) == 2);
  REQUIRE(Enchanted(inv, kIronDagger)->count == 1);
}

TEST_CASE("MadeEnchantment believes only an enchantment of that piece "
          "(Thornswood #4513)",
          "[MadeEnchantment][espm]")
{
  PartOne& p = GetPartOne();
  auto craftService = p.GetActionListener().GetCraftService();
  auto& br = p.GetEspm().GetBrowser();
  const auto dagger =
    Inventory().AddItem(kIronDagger, 1).AddItem(kPettySoulGem, 1);
  const auto helmet =
    Inventory().AddItem(kIronHelmet, 1).AddItem(kPettySoulGem, 1);

  auto ok = craftService->MadeEnchantment(br, dagger, kIronDagger,
                                          Claim(OnDagger(10.0000001f), 250.f));
  REQUIRE(ok.has_value());
  REQUIRE(*ok == OnDagger());
  REQUIRE(craftService->MadeEnchantment(br, helmet, kIronHelmet,
                                        Claim(OnHelmet(), std::nullopt)));

  // Nothing claimed: a client from before #4513.
  REQUIRE(!craftService->MadeEnchantment(br, dagger, kIronDagger,
                                         Claim(std::nullopt, 250.f)));

  // The wrong kind of piece, a piece that did not go in, no soul gem, or
  // something that is not a weapon or armour at all.
  REQUIRE(!craftService->MadeEnchantment(br, helmet, kIronHelmet,
                                         Claim(OnDagger(), 250.f)));
  REQUIRE(!craftService->MadeEnchantment(br, dagger, kIronHelmet,
                                         Claim(OnHelmet(), std::nullopt)));
  REQUIRE(!craftService->MadeEnchantment(
    br, Inventory().AddItem(kIronDagger, 1), kIronDagger,
    Claim(OnDagger(), 250.f)));
  REQUIRE(!craftService->MadeEnchantment(
    br, Inventory().AddItem(kWheat, 1).AddItem(kPettySoulGem, 1), kWheat,
    Claim(OnDagger(), 250.f)));

  // No effect, three, one twice, or one that is not a magic effect.
  auto none = OnDagger();
  none.effects.clear();
  REQUIRE(!craftService->MadeEnchantment(br, dagger, kIronDagger,
                                         Claim(none, 250.f)));
  auto three = OnDagger();
  three.effects.push_back({ kFortifyHealth, 1.f, 0, 1 });
  three.effects.push_back({ 0x39e51, 1.f, 0, 1 });
  REQUIRE(!craftService->MadeEnchantment(br, dagger, kIronDagger,
                                         Claim(three, 250.f)));
  auto twice = OnDagger();
  twice.effects.push_back(twice.effects[0]);
  REQUIRE(!craftService->MadeEnchantment(br, dagger, kIronDagger,
                                         Claim(twice, 250.f)));
  auto notMgef = OnDagger();
  notMgef.effects[0].effectId = kWheat;
  REQUIRE(!craftService->MadeEnchantment(br, dagger, kIronDagger,
                                         Claim(notMgef, 250.f)));

  // Numbers nothing could make, a weapon without a charge, armour with one.
  REQUIRE(!craftService->MadeEnchantment(br, dagger, kIronDagger,
                                         Claim(OnDagger(1e6f), 250.f)));
  REQUIRE(!craftService->MadeEnchantment(
    br, dagger, kIronDagger,
    Claim(OnDagger(std::numeric_limits<float>::quiet_NaN()), 250.f)));
  REQUIRE(!craftService->MadeEnchantment(br, dagger, kIronDagger,
                                         Claim(OnDagger(), std::nullopt)));
  REQUIRE(!craftService->MadeEnchantment(br, dagger, kIronDagger,
                                         Claim(OnDagger(), 1e6f)));
  REQUIRE(!craftService->MadeEnchantment(br, helmet, kIronHelmet,
                                         Claim(OnHelmet(), 250.f)));
}

TEST_CASE("An enchanted piece lands in the pack with its enchantment, and "
          "one that is not believable is refused with the inputs kept "
          "(Thornswood #4513)",
          "[MadeEnchantment][espm]")
{
  PartOne& p = GetPartOne();
  auto& ac = MakeEnchanter(p);
  ac.RemoveAllItems();
  ac.AddItem(kIronDagger, 1);
  ac.AddItem(kPettySoulGem, 2);

  RawMessageData msgData;
  msgData.userId = 0;

  // Refused: the dagger and the gem stay, and nothing enchanted appears.
  const Inventory before = ac.GetInventory();
  p.GetActionListener().OnCraftItem(
    msgData, Enchant(kIronDagger, OnDagger(1e6f), 250.f));
  CHECK(ac.GetInventory() == before);

  p.GetActionListener().OnCraftItem(
    msgData, Enchant(kIronDagger, OnDagger(10.0000001f), 250.f));
  CHECK(ac.GetInventory().GetItemCount(kPettySoulGem) == 1);
  CHECK(ac.GetInventory().GetItemCount(kIronDagger) == 1);
  const auto* piece = Enchanted(ac.GetInventory(), kIronDagger);
  REQUIRE(piece);
  CHECK(*piece->madeEnchantment == OnDagger());
  CHECK(piece->enchantmentId == Inventory::MadeEnchantmentId(OnDagger()));
  CHECK(piece->maxCharge == 250.f);

  // And it survives the save.
  const auto back = ThroughSave(ac.GetChangeForm());
  CHECK(back.inv == ac.GetInventory());

  p.DestroyActor(0xff000000);
  DoDisconnect(p, 0);
}
