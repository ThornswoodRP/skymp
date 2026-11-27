#include "StableIds.h"
#include "FormDesc.h"
#include "Inventory.h"
#include "archives/JsonInputArchive.h"
#include <fmt/format.h>
#include <stdexcept>

namespace {
constexpr uint32_t kCreatedFirst = 0xFF000000;

template <class T>
T FromJson(const nlohmann::json& j)
{
  T res;
  JsonInputArchive ar(j);
  res.Serialize(ar);
  return res;
}

bool Has(const nlohmann::json& object, const char* key)
{
  auto it = object.find(key);
  return it != object.end() && !it->is_null();
}

std::optional<uint32_t> Number(const nlohmann::json& value)
{
  if (value.is_number_unsigned()) {
    auto v = value.get<uint64_t>();
    if (v <= 0xFFFFFFFFull) {
      return static_cast<uint32_t>(v);
    }
  } else if (value.is_number_integer()) {
    auto v = value.get<int64_t>();
    if (v >= 0 && v <= 0xFFFFFFFFll) {
      return static_cast<uint32_t>(v);
    }
  }
  return std::nullopt;
}
}

StableIds::StableIds(espm::LoadOrder loadOrder_,
                     std::vector<std::string> forgetPlugins_)
  : loadOrder(std::move(loadOrder_))
  , forgetPlugins(forgetPlugins_.begin(), forgetPlugins_.end())
{
  for (const auto& name : forgetPlugins) {
    if (loadOrder.FindByFileName(name)) {
      throw std::runtime_error(
        fmt::format("forgetPlugins names {}, which the server loads; a plugin "
                    "that is loaded keeps everything that came from it",
                    name));
    }
  }
}

nlohmann::json StableIds::EncodeId(uint32_t id, const std::string& where,
                                   Notes& notes) const
{
  if (id == 0 || id >= kCreatedFirst) {
    return id;
  }
  auto position = loadOrder.FindByFormId(id);
  if (!position) {
    notes.push_back(fmt::format(
      "{} is {:x}, which no loaded plugin owns; kept as that number", where,
      id));
    return id;
  }
  const auto slot = loadOrder.GetSlot(*position);
  return FormDesc(id & slot.LocalIdMask(), loadOrder[*position]).ToString();
}

std::optional<uint32_t> StableIds::DecodeId(const nlohmann::json& value,
                                            const std::string& where,
                                            Notes& notes) const
{
  if (auto number = Number(value)) {
    return number;
  }
  if (!value.is_string()) {
    throw std::runtime_error(
      fmt::format("{} is {}, which is neither an id nor a plugin's form",
                  where, value.dump()));
  }
  const auto text = value.get<std::string>();
  const auto desc = FormDesc::FromString(text);
  if (desc.file.empty() || desc.ToString() != text) {
    throw std::runtime_error(
      fmt::format("{} is \"{}\", which names no plugin's form", where, text));
  }
  const auto position = loadOrder.FindByFileName(desc.file);
  if (!position) {
    if (forgetPlugins.count(desc.file)) {
      notes.push_back(
        fmt::format("{} was {}; {} is in forgetPlugins, so it was dropped",
                    where, text, desc.file));
      return std::nullopt;
    }
    throw std::runtime_error(fmt::format(
      "{} is {}, but {} is not in the server's load order. Nothing was "
      "changed. Load the plugin again, or name it in forgetPlugins to drop "
      "everything that came from it",
      where, text, desc.file));
  }
  const auto slot = loadOrder.GetSlot(*position);
  if ((desc.shortFormId & ~slot.LocalIdMask()) != 0) {
    throw std::runtime_error(
      fmt::format("{} is {}, which names no form: {} is a {} plugin", where,
                  text, desc.file, slot.light ? "light" : "full"));
  }
  return slot.ToId(desc.shortFormId);
}

void StableIds::EncodeExtra(nlohmann::json& extra, bool hasBaseId,
                            const std::string& where, Notes& notes) const
{
  // Whether the made ids are the hashes of what the entry carries is asked
  // before the effects inside are rewritten.
  bool madeBase = false;
  if (hasBaseId && Has(extra, "made")) {
    auto baseId = Number(extra.at("baseId"));
    madeBase = baseId &&
      *baseId ==
        Inventory::MadeBaseId(FromJson<Inventory::Made>(extra["made"]));
  }
  bool madeEnchantment = false;
  if (Has(extra, "madeEnchantment") && Has(extra, "enchantmentId")) {
    auto enchantmentId = Number(extra.at("enchantmentId"));
    madeEnchantment = enchantmentId &&
      *enchantmentId ==
        Inventory::MadeEnchantmentId(FromJson<Inventory::MadeEnchantment>(
          extra["madeEnchantment"]));
  }

  for (const char* key : { "made", "madeEnchantment" }) {
    if (!Has(extra, key)) {
      continue;
    }
    auto& effects = extra[key]["effects"];
    for (size_t i = 0; i < effects.size(); ++i) {
      auto& effectId = effects[i]["effectId"];
      if (auto id = Number(effectId)) {
        effectId =
          EncodeId(*id, fmt::format("{} {} effect {}", where, key, i), notes);
      }
    }
  }

  if (hasBaseId) {
    auto& baseId = extra["baseId"];
    if (madeBase) {
      baseId = kMade;
    } else if (auto id = Number(baseId)) {
      baseId = EncodeId(*id, where + " baseId", notes);
    }
  }
  if (Has(extra, "enchantmentId")) {
    auto& enchantmentId = extra["enchantmentId"];
    if (madeEnchantment) {
      enchantmentId = kMade;
    } else if (auto id = Number(enchantmentId)) {
      enchantmentId = EncodeId(*id, where + " enchantmentId", notes);
    }
  }
  if (Has(extra, "poisonId")) {
    auto& poisonId = extra["poisonId"];
    if (auto id = Number(poisonId)) {
      poisonId = EncodeId(*id, where + " poisonId", notes);
    }
  }
}

bool StableIds::DecodeExtra(nlohmann::json& extra, bool hasBaseId,
                            const std::string& where, Notes& notes) const
{
  // Effects first: the made ids are hashes over them.
  bool keepMade = true;
  bool keepMadeEnchantment = true;
  for (const char* key : { "made", "madeEnchantment" }) {
    if (!Has(extra, key)) {
      continue;
    }
    auto& effects = extra[key]["effects"];
    bool keep = true;
    for (size_t i = 0; i < effects.size(); ++i) {
      auto& effectId = effects[i]["effectId"];
      auto id = DecodeId(effectId,
                         fmt::format("{} {} effect {}", where, key, i), notes);
      if (id) {
        effectId = *id;
      } else {
        keep = false;
      }
    }
    (std::string(key) == "made" ? keepMade : keepMadeEnchantment) = keep;
  }

  if (!keepMade) {
    // A bottle is what is in it; one of its effects is gone, so it is.
    if (hasBaseId) {
      notes.push_back(where +
                      " was a made bottle with an effect from a "
                      "forgotten plugin, so it was dropped");
      return false;
    }
    extra.erase("made");
  }
  if (!keepMadeEnchantment) {
    // The piece stays; the enchantment made on it goes.
    notes.push_back(where +
                    " lost its made enchantment, which had an "
                    "effect from a forgotten plugin");
    extra.erase("madeEnchantment");
    extra.erase("enchantmentId");
  }

  if (hasBaseId) {
    auto& baseId = extra.at("baseId");
    if (baseId == kMade) {
      if (!Has(extra, "made")) {
        throw std::runtime_error(where +
                                 " has a made baseId but carries no made");
      }
      baseId =
        Inventory::MadeBaseId(FromJson<Inventory::Made>(extra.at("made")));
    } else if (auto id = DecodeId(baseId, where + " baseId", notes)) {
      baseId = *id;
    } else {
      return false;
    }
  }

  if (Has(extra, "enchantmentId")) {
    auto& enchantmentId = extra.at("enchantmentId");
    if (enchantmentId == kMade) {
      if (!Has(extra, "madeEnchantment")) {
        throw std::runtime_error(
          where + " has a made enchantmentId but carries no madeEnchantment");
      }
      enchantmentId = Inventory::MadeEnchantmentId(
        FromJson<Inventory::MadeEnchantment>(extra.at("madeEnchantment")));
    } else if (auto id =
                 DecodeId(enchantmentId, where + " enchantmentId", notes)) {
      enchantmentId = *id;
    } else {
      extra.erase("enchantmentId");
    }
  }

  if (Has(extra, "poisonId")) {
    if (auto id = DecodeId(extra.at("poisonId"), where + " poisonId", notes)) {
      extra["poisonId"] = *id;
    } else {
      extra.erase("poisonId");
      extra.erase("poisonCount");
    }
  }
  return true;
}

void StableIds::EncodeInventory(nlohmann::json& inventory,
                                const std::string& where, Notes& notes) const
{
  if (!inventory.is_object() || !Has(inventory, "entries")) {
    return;
  }
  auto& entries = inventory["entries"];
  for (size_t i = 0; i < entries.size(); ++i) {
    EncodeExtra(entries[i], true, fmt::format("{} entry {}", where, i), notes);
  }
}

void StableIds::DecodeInventory(nlohmann::json& inventory,
                                const std::string& where, Notes& notes) const
{
  if (!inventory.is_object() || !Has(inventory, "entries")) {
    return;
  }
  auto& entries = inventory["entries"];
  auto kept = nlohmann::json::array();
  for (size_t i = 0; i < entries.size(); ++i) {
    if (DecodeExtra(entries[i], true, fmt::format("{} entry {}", where, i),
                    notes)) {
      kept.push_back(std::move(entries[i]));
    }
  }
  entries = std::move(kept);
}

namespace {
const char* const kSpellSlots[] = { "leftSpell", "rightSpell", "voiceSpell",
                                    "instantSpell" };
}

void StableIds::Encode(nlohmann::json& changeForm, Notes& notes) const
{
  const std::string form =
    "form " + changeForm.value("formDesc", std::string("?"));

  if (Has(changeForm, "inv")) {
    EncodeInventory(changeForm["inv"], form + " inv", notes);
  }

  if (Has(changeForm, "equipmentDump")) {
    auto& equipment = changeForm["equipmentDump"];
    if (Has(equipment, "inv")) {
      EncodeInventory(equipment["inv"], form + " equipment", notes);
    }
    for (const char* slot : kSpellSlots) {
      if (Has(equipment, slot)) {
        if (auto id = Number(equipment[slot])) {
          equipment[slot] = EncodeId(*id, form + " " + slot, notes);
        }
      }
    }
  }

  if (Has(changeForm, "learnedSpells")) {
    auto& spells = changeForm["learnedSpells"];
    for (auto& spell : spells) {
      if (auto id = Number(spell)) {
        spell = EncodeId(*id, form + " learned spell", notes);
      }
    }
  }

  if (Has(changeForm, "learnedEnchantments")) {
    for (auto& effect : changeForm["learnedEnchantments"]) {
      if (auto id = Number(effect)) {
        effect = EncodeId(*id, form + " learned enchantment", notes);
      }
    }
  }

  if (Has(changeForm, "appearanceDump")) {
    auto& appearance = changeForm["appearanceDump"];
    for (const char* key : { "raceId", "headTextureSetId" }) {
      if (Has(appearance, key)) {
        if (auto id = Number(appearance[key])) {
          appearance[key] = EncodeId(*id, form + " " + key, notes);
        }
      }
    }
    if (Has(appearance, "headpartIds")) {
      for (auto& part : appearance["headpartIds"]) {
        if (auto id = Number(part)) {
          part = EncodeId(*id, form + " head part", notes);
        }
      }
    }
  }

  if (Has(changeForm, "effects")) {
    for (auto& effect : changeForm["effects"]) {
      if (Has(effect, "effectId")) {
        if (auto id = Number(effect["effectId"])) {
          effect["effectId"] = EncodeId(*id, form + " active effect", notes);
        }
      }
    }
  }

  if (Has(changeForm, "pickupExtra")) {
    EncodeExtra(changeForm["pickupExtra"], false, form + " pickupExtra",
                notes);
  }
}

void StableIds::Decode(nlohmann::json& changeForm, Notes& notes) const
{
  const std::string form =
    "form " + changeForm.value("formDesc", std::string("?"));

  if (Has(changeForm, "inv")) {
    DecodeInventory(changeForm["inv"], form + " inv", notes);
  }

  if (Has(changeForm, "equipmentDump")) {
    auto& equipment = changeForm["equipmentDump"];
    if (Has(equipment, "inv")) {
      DecodeInventory(equipment["inv"], form + " equipment", notes);
    }
    for (const char* slot : kSpellSlots) {
      if (Has(equipment, slot)) {
        if (auto id = DecodeId(equipment[slot], form + " " + slot, notes)) {
          equipment[slot] = *id;
        } else {
          equipment.erase(slot);
        }
      }
    }
  }

  if (Has(changeForm, "learnedSpells")) {
    auto kept = nlohmann::json::array();
    for (auto& spell : changeForm["learnedSpells"]) {
      if (auto id = DecodeId(spell, form + " learned spell", notes)) {
        kept.push_back(*id);
      }
    }
    changeForm["learnedSpells"] = std::move(kept);
  }

  if (Has(changeForm, "learnedEnchantments")) {
    auto kept = nlohmann::json::array();
    for (auto& effect : changeForm["learnedEnchantments"]) {
      if (auto id = DecodeId(effect, form + " learned enchantment", notes)) {
        kept.push_back(*id);
      }
    }
    changeForm["learnedEnchantments"] = std::move(kept);
  }

  if (Has(changeForm, "appearanceDump")) {
    auto& appearance = changeForm["appearanceDump"];
    for (const char* key : { "raceId", "headTextureSetId" }) {
      if (Has(appearance, key)) {
        // A race or a face texture has no stand-in: 0 is none, and the
        // character is shown as the game shows a character with none.
        appearance[key] =
          DecodeId(appearance[key], form + " " + key, notes).value_or(0);
      }
    }
    if (Has(appearance, "headpartIds")) {
      auto kept = nlohmann::json::array();
      for (auto& part : appearance["headpartIds"]) {
        if (auto id = DecodeId(part, form + " head part", notes)) {
          kept.push_back(*id);
        }
      }
      appearance["headpartIds"] = std::move(kept);
    }
  }

  if (Has(changeForm, "effects")) {
    auto kept = nlohmann::json::array();
    for (auto& effect : changeForm["effects"]) {
      if (Has(effect, "effectId")) {
        auto id = DecodeId(effect["effectId"], form + " active effect", notes);
        if (!id) {
          continue;
        }
        effect["effectId"] = *id;
      }
      kept.push_back(std::move(effect));
    }
    changeForm["effects"] = std::move(kept);
  }

  if (Has(changeForm, "pickupExtra")) {
    DecodeExtra(changeForm["pickupExtra"], false, form + " pickupExtra",
                notes);
  }
}
