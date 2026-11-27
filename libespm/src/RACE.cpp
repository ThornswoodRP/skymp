#include "libespm/RACE.h"
#include "libespm/RecordHeaderAccess.h"
#include <cstring>
#include <string>

namespace espm {

RACE::Data RACE::GetData(
  CompressedFieldsCache& compressedFieldsCache) const noexcept
{
  Data result;
  RecordHeaderAccess::IterateFields(
    this,
    [&](const char* type, uint32_t size, const char* data) {
      if (!std::memcmp(type, "DATA", 4)) {
        result.flags = *reinterpret_cast<const uint32_t*>(data + 32);
        result.startingHealth = *reinterpret_cast<const float*>(data + 36);
        result.startingMagicka = *reinterpret_cast<const float*>(data + 40);
        result.startingStamina = *reinterpret_cast<const float*>(data + 44);
        result.healRegen = *reinterpret_cast<const float*>(data + 84);
        result.magickaRegen = *reinterpret_cast<const float*>(data + 88);
        result.staminaRegen = *reinterpret_cast<const float*>(data + 92);
        result.unarmedDamage = *reinterpret_cast<const float*>(data + 96);
        result.unarmedReach = *reinterpret_cast<const float*>(data + 100);
      } else if (!std::memcmp(type, "SPLO", 4)) {
        result.spells.emplace(*reinterpret_cast<const uint32_t*>(data));
      } else if (!std::memcmp(type, "ATKR", 4) && size >= 4) {
        std::memcpy(&result.attackRace, data, 4);
      } else if (!std::memcmp(type, "ATKD", 4) && size >= 16) {
        Data::Attack attack;
        std::memcpy(&attack.flags, data + 12, 4);
        result.attacks.push_back(attack);
      } else if (!std::memcmp(type, "ATKE", 4) && !result.attacks.empty() &&
                 result.attacks.back().event.empty()) {
        result.attacks.back().event = std::string(data, strnlen(data, size));
      }
    },
    compressedFieldsCache);
  return result;
}

}
