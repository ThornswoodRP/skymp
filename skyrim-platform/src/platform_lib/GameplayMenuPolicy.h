#pragma once
#include <atomic>
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

// Request/Entered cross the Node/SKSE boundary. The held Session is accessed
// only by the main UI owner (including the explicit title Show callback).
class World
{
public:
  void Request(bool value) { requested.store(value, std::memory_order_release); }
  void Loading() { entered.store(false, std::memory_order_release); }
  void Loaded(bool success) { entered.store(success, std::memory_order_release); }
  void Title()
  {
    entered.store(false, std::memory_order_release);
    requested.store(false, std::memory_order_release);
    session.Update(false, false);
  }
  bool Eligible(bool player3D, bool titleOpen)
  {
    const bool world = entered.load(std::memory_order_acquire) && player3D && !titleOpen;
    return session.Update(requested.load(std::memory_order_acquire), world);
  }
private:
  std::atomic_bool requested{ false }, entered{ false };
  Session session;
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
