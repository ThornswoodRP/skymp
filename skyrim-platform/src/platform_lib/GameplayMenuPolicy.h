#pragma once
#include <cstdint>
#include <utility>

// The portable part of the native show hook. Skyrim owns its pause counter;
// this policy runs after ProcessMessage(Show), before Skyrim counts the menu.
namespace GameplayMenuPolicy {
constexpr std::uint32_t Pauses = 1u << 0;
constexpr std::uint32_t FrozenBackground = 1u << 5;

inline bool PostLoadSucceeded(void* data)
{
  return data && *static_cast<bool*>(data);
}

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

inline std::uint32_t Flags(std::uint32_t flags, bool worldInSession)
{
  return worldInSession ? flags & ~(Pauses | FrozenBackground) : flags;
}

template <class Callback, class ReadFlags, class WriteFlags>
auto Show(bool worldInSession, Callback&& original, ReadFlags&& read,
          WriteFlags&& write)
{
  const auto result = std::forward<Callback>(original)();
  std::forward<WriteFlags>(write)(Flags(std::forward<ReadFlags>(read)(), worldInSession));
  return result;
}
}
