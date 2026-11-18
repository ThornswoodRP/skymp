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
  // SKSE2.2.6 dispatches (void*)result, not a pointer to a bool. Success is
  // the value 0x1 and must never be dereferenced (Hooks_SaveLoad.cpp:51).
  return data != nullptr;
}

class Session
{
public:
  bool Update(bool requested, bool entered, bool readyToEnter)
  {
    held = entered && (held || (requested && readyToEnter));
    return held;
  }
private:
  bool held = false;
};

// Request/Entered cross the Node/SKSE boundary. The held Session is accessed
// only by the main UI owner's eligibility calculation.
class World
{
public:
  void Request(bool value) { requested.store(value, std::memory_order_release); }
  void Loading()
  {
    entered.store(false, std::memory_order_release);
    unloadEpoch.fetch_add(1, std::memory_order_release);
  }
  void Loaded(bool success) { entered.store(success, std::memory_order_release); }
  void Title()
  {
    Loading(); // a title transition does not destroy an existing client
  }
  bool Eligible(bool player3D, bool titleOpen)
  {
    const auto epoch = unloadEpoch.load(std::memory_order_acquire);
    if (epoch != seenUnload) {
      session.Update(false, false, false);
      seenUnload = epoch;
    }
    return session.Update(requested.load(std::memory_order_acquire),
      entered.load(std::memory_order_acquire), player3D && !titleOpen);
  }
private:
  std::atomic_bool requested{ false }, entered{ false };
  std::atomic<std::uint64_t> unloadEpoch{ 0 };
  std::uint64_t seenUnload = 0;
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
