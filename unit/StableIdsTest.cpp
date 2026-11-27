// The ids inside a saved form are kept by plugin, so a load order change
// cannot shift them (Thornswood #6237, database_drivers/utils/StableIds.h).
// No game data and no database: the load orders are lists of names.
#include "database_drivers/utils/StableIds.h"
#include "Inventory.h"
#include "MpChangeForms.h"
#include "TestUtils.hpp" // DumpChangeForm
#include "libespm/LoadOrder.h"
#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>

using Catch::Matchers::ContainsSubstring;

namespace {
// Each name ending in ".esl" is light, every other one full, the way the
// game counts them: each kind from 0, in load order.
espm::LoadOrder Order(std::vector<std::string> names)
{
  std::vector<espm::PluginSlot> slots;
  uint16_t full = 0, light = 0;
  for (auto& name : names) {
    bool isLight = name.size() > 4 && name.substr(name.size() - 4) == ".esl";
    slots.push_back({ isLight, isLight ? light++ : full++ });
  }
  return espm::LoadOrder(std::move(names), std::move(slots));
}

// The order a world was saved under, and the same plugins after a new full
// plugin went in front of Sword.esp and a new light one in front of
// Beards.esl. Every id from Sword.esp and Beards.esl moves.
const auto kBefore = Order({ "Skyrim.esm", "Sword.esp", "Beards.esl" });
const auto kAfter = Order(
  { "Skyrim.esm", "New.esp", "Sword.esp", "NewLight.esl", "Beards.esl" });

uint32_t Id(const espm::LoadOrder& order, const char* desc)
{
  return FormDesc::FromString(desc).ToFormId(order);
}

Inventory::Made Brew(const espm::LoadOrder& order)
{
  Inventory::Made made;
  made.name = "Sword Oil";
  made.effects.push_back({ Id(order, "3eb15:Skyrim.esm"), 10.f, 0, 30 });
  made.effects.push_back({ Id(order, "812:Sword.esp"), 2.5f, 0, 60 });
  return made;
}

Inventory::MadeEnchantment Enchant(const espm::LoadOrder& order)
{
  Inventory::MadeEnchantment enchantment;
  enchantment.weapon = true;
  enchantment.effects.push_back({ Id(order, "813:Sword.esp"), 5.f, 0, 1 });
  return enchantment;
}

// A character holding something from every kind of plugin, in every field
// that keeps an id, under the given order.
MpChangeForm Character(const espm::LoadOrder& order)
{
  MpChangeForm f;
  f.recType = MpChangeForm::ACHR;
  f.formDesc = FormDesc::FromString("14");
  f.profileId = 42;

  f.inv.AddItem(Id(order, "f:Skyrim.esm"), 300); // gold
  f.inv.AddItem(Id(order, "801:Sword.esp"), 1);

  Inventory::ExtraData poisoned;
  poisoned.poisonId = Id(order, "802:Sword.esp");
  poisoned.poisonCount = 3;
  f.inv.AddItems({ { Id(order, "803:Sword.esp"), 1, poisoned } });

  Inventory::ExtraData bottle;
  bottle.made = Brew(order);
  f.inv.AddItems({ { Inventory::MadeBaseId(*bottle.made), 2, bottle } });

  Inventory::ExtraData enchanted;
  enchanted.madeEnchantment = Enchant(order);
  enchanted.enchantmentId =
    Inventory::MadeEnchantmentId(*enchanted.madeEnchantment);
  enchanted.worn_ = true;
  f.inv.AddItems({ { Id(order, "804:Sword.esp"), 1, enchanted } });

  f.equipment.inv.AddItems({ { Id(order, "804:Sword.esp"), 1, enchanted } });
  f.equipment.leftSpell = Id(order, "805:Sword.esp");
  f.equipment.rightSpell = Id(order, "2b96b:Skyrim.esm");

  f.learnedSpells.LearnSpell(Id(order, "805:Sword.esp"));
  f.learnedSpells.LearnSpell(Id(order, "12fcd:Skyrim.esm"));

  nlohmann::json appearance = {
    { "isFemale", false },
    { "raceId", Id(order, "13746:Skyrim.esm") },
    { "weight", 50 },
    { "skinColor", 0 },
    { "hairColor", 0 },
    { "headpartIds",
      { Id(order, "827:Beards.esl"), Id(order, "838:Beards.esl"),
        Id(order, "e44:Skyrim.esm") } },
    { "headTextureSetId", Id(order, "806:Sword.esp") },
    { "options", nlohmann::json::array() },
    { "presets", nlohmann::json::array() },
    { "tints", nlohmann::json::array() },
    { "name", "Brynja" }
  };
  f.appearanceDump = appearance.dump();
  return f;
}

std::string Stored(const MpChangeForm& f, const StableIds& ids)
{
  auto j = MpChangeForm::ToJson(f);
  StableIds::Notes notes;
  ids.Encode(j, notes);
  REQUIRE(notes.empty());
  return j.dump();
}

MpChangeForm Loaded(const std::string& stored, const StableIds& ids,
                    StableIds::Notes* outNotes = nullptr)
{
  auto j = nlohmann::json::parse(stored);
  StableIds::Notes notes;
  ids.Decode(j, notes);
  if (outNotes) {
    *outNotes = notes;
  }
  simdjson::dom::parser parser;
  auto dump = j.dump();
  auto element = parser.parse(dump).value();
  return MpChangeForm::JsonToChangeForm(element);
}

}

TEST_CASE("Stable ids: a save reads back the same under the same order",
          "[StableIds]")
{
  StableIds ids(kBefore, {});
  auto f = Character(kBefore);
  REQUIRE(DumpChangeForm(Loaded(Stored(f, ids), ids)) == DumpChangeForm(f));
}

TEST_CASE("Stable ids: a load order change keeps every item, spell, head "
          "part, race and made id on the same form",
          "[StableIds]")
{
  auto stored = Stored(Character(kBefore), StableIds(kBefore, {}));

  // What the database keeps names plugins, not load order positions.
  REQUIRE_THAT(stored, ContainsSubstring("\"801:Sword.esp\""));
  REQUIRE_THAT(stored, ContainsSubstring("\"827:Beards.esl\""));
  REQUIRE_THAT(stored, ContainsSubstring("\"baseId\":\"made\""));
  REQUIRE_THAT(stored, ContainsSubstring("\"enchantmentId\":\"made\""));

  // Read under the new order, it is the character as the new order gives
  // it: every id moved with its plugin, and the made ids are the hashes of
  // the moved effects.
  auto loaded = Loaded(stored, StableIds(kAfter, {}));
  auto expected = Character(kAfter);
  REQUIRE(DumpChangeForm(loaded) == DumpChangeForm(expected));

  // And it is not what the old ids would have said.
  REQUIRE(Id(kAfter, "801:Sword.esp") != Id(kBefore, "801:Sword.esp"));
  REQUIRE(Id(kAfter, "827:Beards.esl") != Id(kBefore, "827:Beards.esl"));
  REQUIRE(Inventory::MadeBaseId(Brew(kAfter)) !=
          Inventory::MadeBaseId(Brew(kBefore)));
}

TEST_CASE("Stable ids: a world saved with numbers reads as it always did",
          "[StableIds]")
{
  auto f = Character(kBefore);
  auto legacy = MpChangeForm::ToJson(f).dump();
  REQUIRE_THAT(legacy, !ContainsSubstring("Sword.esp\""));
  REQUIRE(DumpChangeForm(Loaded(legacy, StableIds(kBefore, {}))) == DumpChangeForm(f));
}

TEST_CASE("Stable ids: ids no plugin owns stay numbers", "[StableIds]")
{
  StableIds ids(kBefore, {});
  MpChangeForm f;
  f.formDesc = FormDesc::FromString("123");
  f.inv.AddItem(0xff000456, 1); // a form the server made
  f.inv.AddItem(Id(kBefore, "f:Skyrim.esm"), 5);
  f.inv.AddItem(0x05000801, 1); // full index 5: no plugin there
  auto j = MpChangeForm::ToJson(f);
  StableIds::Notes notes;
  ids.Encode(j, notes);
  REQUIRE(j["inv"]["entries"][0]["baseId"] == 0xff000456);
  REQUIRE(j["inv"]["entries"][1]["baseId"] == "f:Skyrim.esm");
  REQUIRE(j["inv"]["entries"][2]["baseId"] == 0x05000801);
  REQUIRE(notes.size() == 1);
  REQUIRE_THAT(notes[0], ContainsSubstring("5000801"));
}

TEST_CASE("Stable ids: a plugin that is gone stops the load and says which",
          "[StableIds]")
{
  auto stored = Stored(Character(kBefore), StableIds(kBefore, {}));
  auto withoutSword = Order({ "Skyrim.esm", "Beards.esl" });
  REQUIRE_THROWS_WITH(Loaded(stored, StableIds(withoutSword, {})),
                      ContainsSubstring("Sword.esp is not in the server's "
                                        "load order"));
}

TEST_CASE("Stable ids: a forgotten plugin's items, spells and parts are "
          "dropped and said",
          "[StableIds]")
{
  auto stored = Stored(Character(kBefore), StableIds(kBefore, {}));
  auto withoutSword = Order({ "Skyrim.esm", "Beards.esl" });
  StableIds::Notes notes;
  auto f = Loaded(stored, StableIds(withoutSword, { "Sword.esp" }), &notes);
  REQUIRE(!notes.empty());

  // Gold stays; the sword, the poisoned piece and the bottle with a Sword.esp
  // effect go. The enchanted piece is from Sword.esp too.
  REQUIRE(f.inv.entries.size() == 1);
  REQUIRE(f.inv.entries[0].baseId == Id(withoutSword, "f:Skyrim.esm"));
  REQUIRE(f.inv.entries[0].count == 300);

  REQUIRE(!f.equipment.leftSpell.has_value());
  REQUIRE(f.equipment.rightSpell == Id(withoutSword, "2b96b:Skyrim.esm"));
  REQUIRE(f.learnedSpells.GetLearnedSpells() ==
          std::vector<uint32_t>{ Id(withoutSword, "12fcd:Skyrim.esm") });

  auto appearance = nlohmann::json::parse(f.appearanceDump);
  REQUIRE(appearance["headpartIds"].size() == 3);
  REQUIRE(appearance["headTextureSetId"] == 0);
}

TEST_CASE("Stable ids: forgetting a plugin the server loads is refused",
          "[StableIds]")
{
  REQUIRE_THROWS_WITH(StableIds(kBefore, { "Sword.esp" }),
                      ContainsSubstring("which the server loads"));
}
