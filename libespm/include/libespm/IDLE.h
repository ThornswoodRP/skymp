#pragma once
#include "RecordHeader.h"
#include "libespm/CTDA.h"
#include <string>
#include <vector>

#pragma pack(push, 1)

namespace espm {

// An idle animation: one node of the game's idle tree. Thornswood #4507: the
// killmoves are idles in this tree (under AttackRightRoot and PowerAttackRoot,
// by way of KillMoveFrontSideRoot and the like), and which one fits a kill is
// in the conditions on an idle and on each of its parents.
class IDLE final : public RecordHeader
{
public:
  static constexpr auto kType = "IDLE";

  struct Condition
  {
    CTDA ctda;
    // CIS1 and CIS2: a condition's string parameters, for the functions that
    // take one (GetGraphVariableInt names its graph variable in CIS1). The
    // CTDA's own parameter holds no meaning then.
    std::string firstStringParameter;
    std::string secondStringParameter;
  };

  struct Data
  {
    std::vector<Condition> conditions;
    std::string behaviorGraph;  // DNAM, empty when the record has none
    std::string animationEvent; // ENAM, empty for a node that only groups
    // ANAM, raw form ids as the file stores them (its own masters' order)
    uint32_t parentId = 0;
    uint32_t previousId = 0;
    // DATA
    uint8_t loopingMin = 0;
    uint8_t loopingMax = 0;
    uint8_t flags = 0;
    uint8_t animationGroupSection = 0;
    uint16_t replayDelay = 0;
  };

  Data GetData(CompressedFieldsCache& compressedFieldsCache) const noexcept;
};

static_assert(sizeof(IDLE) == sizeof(RecordHeader));

}

#pragma pack(pop)
