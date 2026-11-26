#pragma once
#include "libespm/LoadOrder.h"
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <string>
#include <vector>

// Thornswood #6237: the ids inside a saved change form, kept by the plugin
// that owns them instead of by where that plugin loads.
//
// A form's own keys are kept as descriptors already ("3a4b:Skyrim.esm" for
// formDesc, baseDesc, worldOrCellDesc and the rest), so a load order change
// cannot move the form itself. What the form holds was kept as the raw id
// the game uses this session: the top byte (or, for a light plugin, the 12
// bit index under 0xFE) is the plugin's place in the load order. Add a
// plugin in front of another, or take one out, and every item, spell, head
// part, race and magic effect from the plugins after it pointed at some other
// form, or at none.
//
// Encode rewrites those ids as descriptors, the way the form's keys are
// written, in the document the driver stores. Decode turns them back into the
// ids this load order gives them. Numbers Decode finds are left as they are,
// so a world saved before this reads the same, and its next save of each form
// writes it with descriptors.
//
// The fields, all of them that hold a plugin's form id:
//   inv.entries[]          baseId, enchantmentId, poisonId,
//                          made.effects[].effectId,
//                          madeEnchantment.effects[].effectId
//   equipmentDump          inv.entries[] (as above), leftSpell, rightSpell,
//                          voiceSpell, instantSpell
//   learnedSpells[]
//   learnedEnchantments[]  (Thornswood #4579)
//   appearanceDump         raceId, headpartIds[], headTextureSetId
//   effects[]              effectId
//   pickupExtra            enchantmentId, poisonId, made and
//                          madeEnchantment effects
// dynamicFields belong to the gamemode, which writes what it likes there.
//
// Ids that stay numbers: 0 (none), and ids at or above 0xFF000000, which the
// server or a game session made and no plugin owns. One exception, the made
// ids: a brewed bottle's baseId is Inventory::MadeBaseId of what is in it,
// and a made enchantment's enchantmentId is Inventory::MadeEnchantmentId of
// it, hashes over its magic effects' raw ids. A load order change changes
// those raw ids, so the hash has to change with them. Encode writes "made"
// in place of such an id when it is the hash of what the entry carries, and
// Decode puts back the hash of the decoded effects.
//
// A descriptor whose plugin is not loaded stops the load: the world is left
// as it was and the error names the form, the field and the plugin. A
// plugin taken out on purpose is named in forgetPlugins, and then what came
// from it is dropped (an item, a spell, a head part, an effect), each one
// said in the notes the driver logs.
class StableIds
{
public:
  StableIds(espm::LoadOrder loadOrder, std::vector<std::string> forgetPlugins);

  // What Encode or Decode did that the log should say.
  using Notes = std::vector<std::string>;

  // In place, on a document MpChangeForm::ToJson wrote.
  void Encode(nlohmann::json& changeForm, Notes& notes) const;

  // In place, before MpChangeForm::JsonToChangeForm reads it. Throws when a
  // descriptor names a plugin that is neither loaded nor forgotten, or names
  // no form.
  void Decode(nlohmann::json& changeForm, Notes& notes) const;

  const espm::LoadOrder& GetLoadOrder() const noexcept { return loadOrder; }

  static constexpr const char* kMade = "made";

private:
  void EncodeExtra(nlohmann::json& extra, bool hasBaseId,
                   const std::string& where, Notes& notes) const;
  bool DecodeExtra(nlohmann::json& extra, bool hasBaseId,
                   const std::string& where, Notes& notes) const;
  void EncodeInventory(nlohmann::json& inventory, const std::string& where,
                       Notes& notes) const;
  void DecodeInventory(nlohmann::json& inventory, const std::string& where,
                       Notes& notes) const;

  nlohmann::json EncodeId(uint32_t id, const std::string& where,
                          Notes& notes) const;
  // Nothing when the id came from a forgotten plugin.
  std::optional<uint32_t> DecodeId(const nlohmann::json& value,
                                   const std::string& where,
                                   Notes& notes) const;

  const espm::LoadOrder loadOrder;
  const std::set<std::string> forgetPlugins;
};
