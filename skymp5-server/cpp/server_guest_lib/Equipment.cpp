#include "Equipment.h"
#include "archives/JsonInputArchive.h"
#include "archives/JsonOutputArchive.h"
#include "archives/SimdJsonInputArchive.h"
#include <fmt/format.h>
#include <string>
#include <utility>

bool Equipment::IsSpellEquipped(const uint32_t spellFormId) const
{
  return spellFormId == leftSpell || spellFormId == rightSpell ||
    spellFormId == voiceSpell || spellFormId == instantSpell;
}

nlohmann::json Equipment::ToJson() const
{
  JsonOutputArchive ar;
  const_cast<Equipment*>(this)->Serialize(ar);
  return std::move(ar.j);
}

Equipment Equipment::FromJson(const simdjson::dom::element& element)
{
  SimdJsonInputArchive ar(element);
  Equipment res;
  res.Serialize(ar);
  return res;
}

Equipment Equipment::FromJson(const nlohmann::json& element)
{
  JsonInputArchive ar(element);
  Equipment res;
  res.Serialize(ar);
  return res;
}

std::string Equipment::WornList() const
{
  std::string res = "[";
  bool first = true;
  for (const auto& entry : inv.entries) {
    const auto worn = entry.GetWorn();
    if (worn == Inventory::Worn::None) {
      continue;
    }
    if (!first) {
      res += ",";
    }
    first = false;
    res += fmt::format("{:x}", entry.baseId);
    if (worn == Inventory::Worn::Left) {
      res += "L";
    }
  }
  res += "]";
  const std::pair<const char*, const std::optional<uint32_t>*> spells[] = {
    { "left", &leftSpell },
    { "right", &rightSpell },
    { "voice", &voiceSpell },
    { "instant", &instantSpell }
  };
  for (const auto& [hand, spell] : spells) {
    if (spell->value_or(0) != 0) {
      res += fmt::format(" {}={:x}", hand, spell->value());
    }
  }
  return res;
}
