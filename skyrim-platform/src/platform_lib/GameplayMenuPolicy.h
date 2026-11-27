#pragma once
#include <atomic>
#include <cstdint>
#include <string_view>
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
  bool HasLoadedWorld() const { return entered.load(std::memory_order_acquire); }
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

// THE FACE MENU KEEPS ITS PAUSE (Thornswood #6979). The RaceSex Menu brings
// its own camera round to the character's front only while the game is
// paused under it. With its pause taken off (this policy, 17 Nov) it opened
// on the camera it found: first person, or behind the character's back.
// Patrick's Dev start at 5:59 PM on 26 Nov logged "heading 180.0, camera
// state 0" as it opened and "pauses 0" while it stood open, and his
// screenshots show the hall's door with the face sliders beside it. The real
// game run on build-1.3.89 (16 Nov, before this policy) caught the camera
// already turning off the character's heading one second after the menu
// opened; every run since shows it still looking straight ahead. Pausing it
// costs nothing: a character in the face menu is standing in the creation
// hall and has nothing in the world to keep up with.
inline bool KeepsPause(std::string_view menu)
{
  return menu == "RaceSex Menu";
}

// Whether Show takes the pause off this menu: a live world, and not the face menu.
inline bool Unpauses(bool worldInSession, std::string_view menu)
{
  return worldInSession && !KeepsPause(menu);
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
