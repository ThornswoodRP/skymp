#include "TestUtils.hpp"
#include "libespm/IterateFields.h"
#include "libespm/RACE.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

PartOne& GetPartOne();

// Thornswood #6720, a measurement only: prints each RACE record's attack
// list (ATKR, then each ATKD's flags with its ATKE event) from the plugins
// the unit tests load, so the attack event a server NPC is sent can be
// checked against the events its race's behavior really has. Never fails.
TEST_CASE("Measure: each race's attack events (Thornswood #6720)",
          "[RaceAttackMeasure][espm]")
{
  auto& p = GetPartOne();
  auto& cache = p.worldState.GetEspmCache();
  // GetDistinctRecordsByType does not index RACE, so every form id of the
  // five masters is looked up and the races are kept.
  std::vector<espm::LookupResult> races;
  for (uint32_t file = 0; file < 5; ++file) {
    const uint32_t last = file == 0 ? 0x120000 : 0x80000;
    for (uint32_t local = 0; local < last; ++local) {
      auto lookup = p.GetEspm().GetBrowser().LookupById((file << 24) | local);
      if (lookup.rec && lookup.rec->GetType() == espm::RACE::kType) {
        races.push_back(lookup);
      }
    }
  }
  std::printf("6720-races %zu\n", races.size());
  for (const auto& lookup : races) {
    std::string line;
    uint32_t pendingFlags = 0;
    bool pending = false;
    uint32_t attackRace = 0;
    espm::IterateFields_(
      lookup.rec,
      [&](const char* type, uint32_t size, const char* data) {
        if (!std::memcmp(type, "ATKR", 4) && size >= 4) {
          std::memcpy(&attackRace, data, 4);
        } else if (!std::memcmp(type, "ATKD", 4) && size >= 16) {
          std::memcpy(&pendingFlags, data + 12, 4);
          pending = true;
        } else if (!std::memcmp(type, "ATKE", 4)) {
          line += " ";
          line += std::string(data, strnlen(data, size));
          if (pending) {
            char flags[16];
            std::snprintf(flags, sizeof(flags), "/%x", pendingFlags);
            line += flags;
          }
          pending = false;
        }
      },
      cache);
    std::printf("6720-race %08x %s atkr=%08x:%s\n",
                lookup.ToGlobalId(lookup.rec->GetId()),
                lookup.rec->GetEditorId(cache) ? lookup.rec->GetEditorId(cache) : "",
                attackRace ? lookup.ToGlobalId(attackRace) : 0, line.c_str());
  }
  std::fflush(stdout);
}
