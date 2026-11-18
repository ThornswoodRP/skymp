#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace espm::TemperatureFood {
// Thornswood #4612. The food table keeps these three for their frost
// resistance, not the recovery effects on the same records.
inline constexpr std::string_view kPlugin = "WindhelmSSE.esp";
inline constexpr std::array<uint32_t, 3> kLocalIds{
  0x12010f, 0x12f9c4, 0x17167b,
};

template <class Effects, class IsFrost, class Remove>
void KeepFrost(Effects& effects, IsFrost isFrost, Remove remove)
{
  auto it = effects.begin();
  while (it != effects.end()) {
    if (isFrost(*it)) {
      ++it;
    } else {
      auto removed = *it;
      it = effects.erase(it);
      remove(removed);
    }
  }
}
}