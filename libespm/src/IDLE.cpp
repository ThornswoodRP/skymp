#include "libespm/IDLE.h"
#include "libespm/RecordHeaderAccess.h"
#include <cstring>

namespace espm {

namespace {
std::string ReadZString(const char* data, uint32_t dataSize)
{
  return std::string(data, strnlen(data, dataSize));
}
}

IDLE::Data IDLE::GetData(
  CompressedFieldsCache& compressedFieldsCache) const noexcept
{
  Data result;
  RecordHeaderAccess::IterateFields(
    this,
    [&](const char* type, uint32_t dataSize, const char* data) {
      if (!std::memcmp(type, "CTDA", 4)) {
        if (dataSize >= sizeof(CTDA)) {
          Condition condition;
          std::memcpy(&condition.ctda, data, sizeof(CTDA));
          result.conditions.push_back(std::move(condition));
        }
      } else if (!std::memcmp(type, "CIS1", 4)) {
        // A string parameter belongs to the CTDA right before it
        if (!result.conditions.empty()) {
          result.conditions.back().firstStringParameter =
            ReadZString(data, dataSize);
        }
      } else if (!std::memcmp(type, "CIS2", 4)) {
        if (!result.conditions.empty()) {
          result.conditions.back().secondStringParameter =
            ReadZString(data, dataSize);
        }
      } else if (!std::memcmp(type, "DNAM", 4)) {
        result.behaviorGraph = ReadZString(data, dataSize);
      } else if (!std::memcmp(type, "ENAM", 4)) {
        result.animationEvent = ReadZString(data, dataSize);
      } else if (!std::memcmp(type, "ANAM", 4)) {
        if (dataSize >= 8) {
          std::memcpy(&result.parentId, data, 4);
          std::memcpy(&result.previousId, data + 4, 4);
        }
      } else if (!std::memcmp(type, "DATA", 4)) {
        if (dataSize >= 6) {
          result.loopingMin = static_cast<uint8_t>(data[0]);
          result.loopingMax = static_cast<uint8_t>(data[1]);
          result.flags = static_cast<uint8_t>(data[2]);
          result.animationGroupSection = static_cast<uint8_t>(data[3]);
          std::memcpy(&result.replayDelay, data + 4, 2);
        }
      }
    },
    compressedFieldsCache);
  return result;
}

}
