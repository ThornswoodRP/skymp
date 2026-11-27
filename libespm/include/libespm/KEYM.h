#pragma once
#include "RecordHeader.h"

#pragma pack(push, 1)

namespace espm {

// A key: an item like a MISC, sorted under Keys in the game's inventory.
// Its DATA is a value and a weight, laid out as a MISC's is.
class KEYM final : public RecordHeader
{
public:
  static constexpr auto kType = "KEYM";

  struct Data
  {
    float weight = 0.f;
  };

  Data GetData(CompressedFieldsCache& compressedFieldsCache) const;
};

static_assert(sizeof(KEYM) == sizeof(RecordHeader));

}

#pragma pack(pop)
