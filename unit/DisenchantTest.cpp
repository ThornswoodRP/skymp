// THORNSWOOD PATCH (Thornswood #4579). A disenchant at an arcane enchanter
// takes the piece on the server and keeps what it taught across a save.
//
// Disenchanting takes a piece out of the pack inside the crafting menu and
// teaches its enchantment, and nothing comes back, so no craft was ever sent.
// The server heard nothing: its inventory put the piece back when the menu
// closed, and what was learned lived only in the client's game, which a
// session starts fresh. The client now sends a "disenchant" custom packet
// naming the bench and the piece, CraftService::Disenchant checks it, takes
// the piece and puts the enchantment's effects on the change form
// (MpChangeForm::learnedEnchantments), and the arrival names them back.
//
// The enchanter and the enchanted weapon are found in Skyrim.esm by what they
// are (a bench carrying CraftingMagicEnchanter; a weapon whose own record
// carries an enchantment), not by ids typed here.
#include "TestUtils.hpp"
#include <catch2/catch_all.hpp>

#include "ActionListener.h"
#include "CraftService.h"
#include "CustomPacketMessage.h"
#include "MpChangeForms.h"
#include "libespm/ENCH.h"
#include "libespm/REFR.h"
#include "libespm/WEAP.h"
#include <cstring>
#include <nlohmann/json.hpp>
#include <set>

PartOne& GetPartOne();

namespace {
constexpr uint32_t kForge = 0x1ad6e;
constexpr uint32_t kIronDagger = 0x1397e;
constexpr uint32_t kMe = 0xff000000;

MpChangeForm ThroughSave(const MpChangeForm& changeForm)
{
  static simdjson::dom::parser parser;
  const std::string dump = MpChangeForm::ToJson(changeForm).dump();
  simdjson::dom::element element = parser.parse(dump).value();
  return MpChangeForm::JsonToChangeForm(element);
}

// The first reference in the load order whose base is an arcane enchanter.
uint32_t FindEnchanter(PartOne& p)
{
  auto& br = p.GetEspm().GetBrowser();
  auto& cache = p.worldState.GetEspmCache();
  for (const auto& lookup : br.GetDistinctRecordsByType("REFR")) {
    const auto data =
      reinterpret_cast<const espm::REFR*>(lookup.rec)->GetData(cache);
    const auto base = br.LookupById(lookup.ToGlobalId(data.baseId));
    if (!base.rec || base.rec->GetType() != "FURN") {
      continue;
    }
    for (uint32_t keywordId : base.rec->GetKeywordIds(cache)) {
      const auto keyword = br.LookupById(keywordId).rec;
      const char* editorId = keyword ? keyword->GetEditorId(cache) : nullptr;
      if (editorId && !std::strcmp(editorId, "CraftingMagicEnchanter")) {
        return lookup.ToGlobalId(lookup.rec->GetId());
      }
    }
  }
  return 0;
}

// The first weapon whose own record carries an enchantment, and that
// enchantment's effects, read here straight off its ENCH record.
std::pair<uint32_t, std::set<uint32_t>> FindEnchantedWeapon(PartOne& p)
{
  auto& br = p.GetEspm().GetBrowser();
  auto& cache = p.worldState.GetEspmCache();
  for (const auto& lookup : br.GetDistinctRecordsByType("WEAP")) {
    const auto local = reinterpret_cast<const espm::WEAP*>(lookup.rec)
                         ->GetData(cache)
                         .enchantmentFormId;
    if (!local) {
      continue;
    }
    const auto ench = br.LookupById(lookup.ToGlobalId(local));
    if (!ench.rec || ench.rec->GetType() != "ENCH") {
      continue;
    }
    std::set<uint32_t> effects;
    for (const auto& effect :
         reinterpret_cast<const espm::ENCH*>(ench.rec)->GetData(cache).effects) {
      effects.insert(ench.ToGlobalId(effect.effectId));
    }
    if (!effects.empty()) {
      return { lookup.ToGlobalId(lookup.rec->GetId()), effects };
    }
  }
  return { 0, {} };
}

void SendDisenchant(PartOne& p, uint32_t workbench, uint32_t baseId)
{
  CustomPacketMessage msg;
  msg.contentJsonDump = nlohmann::json{ { "customPacketType", "disenchant" },
                                        { "workbench", workbench },
                                        { "baseId", baseId } }
                          .dump();
  RawMessageData msgData;
  msgData.userId = 0;
  p.GetActionListener().OnCustomPacket(msgData, msg);
}
}

TEST_CASE("A weapon record's own enchantment is read, and a plain one has "
          "none (Thornswood #4579)",
          "[Disenchant][espm]")
{
  PartOne& p = GetPartOne();
  auto craftService = p.GetActionListener().GetCraftService();
  auto& br = p.GetEspm().GetBrowser();

  const auto [weapon, effects] = FindEnchantedWeapon(p);
  REQUIRE(weapon);
  const auto read = craftService->DisenchantEffects(br, weapon);
  REQUIRE(read.has_value());
  REQUIRE(std::set<uint32_t>(read->begin(), read->end()) == effects);

  REQUIRE(!craftService->DisenchantEffects(br, kIronDagger));
  REQUIRE(craftService->IsEnchanter(FindEnchanter(p)));
  REQUIRE(!craftService->IsEnchanter(kForge));
}

TEST_CASE("A disenchant at an arcane enchanter takes the piece and the "
          "effect it taught is kept across a save (Thornswood #4579)",
          "[Disenchant][espm]")
{
  PartOne& p = GetPartOne();
  const uint32_t enchanter = FindEnchanter(p);
  REQUIRE(enchanter);
  const auto [weapon, effects] = FindEnchantedWeapon(p);
  REQUIRE(weapon);

  auto& bench = p.worldState.GetFormAt<MpObjectReference>(enchanter);
  DoConnect(p, 0);
  p.CreateActor(kMe, bench.GetPos(), 0,
                bench.GetCellOrWorld().ToFormId(p.worldState.espmFiles));
  p.SetUserActor(0, kMe);
  auto& ac = p.worldState.GetFormAt<MpActor>(kMe);
  ac.RemoveAllItems();
  ac.AddItem(weapon, 1);
  ac.AddItem(kIronDagger, 1);

  // Refused, with nothing changed: a piece with no enchantment of its own, a
  // piece the pack does not hold, and a bench that is no enchanter.
  SendDisenchant(p, enchanter, kIronDagger);
  ac.RemoveItems({ Inventory::Entry(weapon, 1) });
  SendDisenchant(p, enchanter, weapon);
  ac.AddItem(weapon, 1);
  SendDisenchant(p, kForge, weapon);
  CHECK(ac.GetInventory().GetItemCount(kIronDagger) == 1);
  CHECK(ac.GetLearnedEnchantments().empty());

  // The one that holds: the piece is gone and the effects are learned.
  REQUIRE(ac.GetInventory().GetItemCount(weapon) == 1);
  SendDisenchant(p, enchanter, weapon);
  CHECK(ac.GetInventory().GetItemCount(weapon) == 0);
  CHECK(ac.GetInventory().GetItemCount(kIronDagger) == 1);
  CHECK(ac.GetLearnedEnchantments() == effects);

  // Kept across a save, and a save with none writes no field for it.
  const auto back = ThroughSave(ac.GetChangeForm());
  CHECK(back.learnedEnchantments == effects);
  CHECK(back.inv.GetItemCount(weapon) == 0);
  MpChangeForm none;
  CHECK(!MpChangeForm::ToJson(none).contains("learnedEnchantments"));
  CHECK(ThroughSave(none).learnedEnchantments.empty());

  p.DestroyActor(kMe);
  DoDisconnect(p, 0);
}
