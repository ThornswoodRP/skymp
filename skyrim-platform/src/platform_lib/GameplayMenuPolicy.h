#pragma once
#include <cstdint>
#include <utility>

// The portable part of the native show hook. Skyrim owns its pause counter;
// this policy runs after ProcessMessage(Show), before Skyrim counts the menu.
namespace GameplayMenuPolicy {
constexpr std::uint32_t Pauses = 1u << 0;
constexpr std::uint32_t FrozenBackground = 1u << 5;

class Session
{
public:
  bool Update(bool requested, bool worldLoaded)
  {
    held = requested || (held && worldLoaded);
    return held && worldLoaded;
  }
private:
  bool held = false;
};

inline std::uint32_t Flags(std::uint32_t flags, bool /*worldInSession*/)
{
  return flags;
}

template <class Menu, class Callback>
auto Show(Menu& menu, bool worldInSession, Callback&& original)
{
  const auto result = std::forward<Callback>(original)();
  menu.flags = Flags(menu.flags, worldInSession);
  return result;
}
}
