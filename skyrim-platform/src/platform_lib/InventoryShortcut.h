#pragma once

#include <cstdint>

namespace InventoryShortcut {

constexpr bool Matches(bool keyboard, bool down, std::uint32_t code,
                       std::uint32_t mapped)
{
  return keyboard && down && mapped > 0 && mapped < 255 && code == mapped;
}

constexpr std::uint32_t NewLifetime(std::uint32_t before, bool open)
{
  return ((before + 2u) & ~1u) | static_cast<std::uint32_t>(open);
}

constexpr bool Current(std::uint32_t requested, std::uint32_t onStack)
{
  return (requested & 1u) != 0 && requested == onStack;
}

constexpr std::uint32_t Request(std::uint32_t onStack, std::uint32_t ready,
                               std::uint32_t code, std::uint32_t mapped)
{
  return Current(ready, onStack) && Matches(true, true, code, mapped) ? ready : 0;
}

}
