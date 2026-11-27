// Thornswood #6714. A tester's /tp to Dead Drop Mine and to Damphall Mine
// failed in Main Playtest #6. The gamemode's /tp finds both names on the real
// tables (Thornswood PR #7128) and then asks the engine where each door of the
// mine stands, through mp.get(id, 'pos') and mp.get(id, 'worldOrCellDesc').
// Every door of both mines is in a DLC master. This asks the engine the same
// thing for each of them, on the five masters, and prints what it answers.
#include "MpObjectReference.h"
#include "PartOne.h"
#include "TestUtils.hpp"
#include <iostream>
#include <sstream>

extern espm::Loader& GetEspmLoader();

TEST_CASE("Thornswood #6714: what the engine answers for the mines' doors",
          "[espm][thornswood-6714]")
{
  PartOne p;
  p.AttachEspm(&GetEspmLoader());

  struct Door
  {
    const char* desc;
    const char* mine;
  };
  const Door doors[] = {
    { "13258:Dawnguard.esm", "Dead Drop Mine" },
    { "13267:Dawnguard.esm", "Dead Drop Mine" },
    { "2491b:Dragonborn.esm", "Damphall Mine" },
    { "25241:Dragonborn.esm", "Damphall Mine" },
    { "25345:Dragonborn.esm", "Damphall Mine" },
    { "253cb:Dragonborn.esm", "Damphall Mine" },
    { "29a0f:Dragonborn.esm", "Damphall Mine" },
    { "29a23:Dragonborn.esm", "Damphall Mine" },
  };

  int found = 0;
  for (const auto& d : doors) {
    const auto id = FormDesc::FromString(d.desc).ToFormId(p.worldState.espmFiles);
    std::stringstream trace;
    const auto& form = p.worldState.LookupFormById(id, &trace);
    auto refr = dynamic_cast<MpObjectReference*>(form.get());
    std::cout << "MEASURE6714 " << d.mine << " door " << d.desc << " id "
              << std::hex << id << std::dec << ": ";
    if (!refr) {
      std::string lines = trace.str();
      for (auto& c : lines) {
        if (c == '\n') c = '|';
      }
      std::cout << "the engine has no reference; trace " << lines
                << std::endl;
      continue;
    }
    ++found;
    const auto& pos = refr->GetPos();
    std::cout << "base " << std::hex << refr->GetBaseId() << std::dec << " "
              << refr->GetBaseType() << ", in "
              << refr->GetCellOrWorld().ToString() << " at " << pos.x << " "
              << pos.y << " " << pos.z << std::endl;
  }
  std::cout << "MEASURE6714 " << found << " of " << std::size(doors)
            << " doors load" << std::endl;
  CHECK(found == static_cast<int>(std::size(doors)));
}
