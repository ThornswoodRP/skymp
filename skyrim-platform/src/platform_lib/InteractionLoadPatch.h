#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

/*
  THORNSWOOD (Thornswood #6884). A FURNITURE ANIMATION LOAD THAT OUTLIVES
  ITS LISTENER.

  Two testers on two machines crashed in the same game code on 21 and
  25 November 2026: an access violation at SkyrimSE.exe+0BCCE21 (Address
  Library id 63997+0xC1, `call [rax+8]`), on a job thread, with
  AnimationFileManagerSingleton named. Both came a fraction of a second after
  walking into The Drunken Huntsman, as a fresh copy of Elrindir leaned on its
  counter (a CounterLeanMarker furniture).

  What 63997 does: the animation file manager tells each finished load its
  listener, through the listener's slot 1. The crashing request had id 4 (RDX
  in both logs), which only 39291 makes: an actor's furniture interaction
  load, listener an InstantInteractionCallback held by the actor's process.

  What frees it with the load still queued: 39798, which ends the process's
  interaction. Under the process lock it looks at slot 4 of the process's
  animation sets. When that slot still holds a delivered set (count != 0), it
  clears the set and jumps past the cancel of the queued request; only when
  the slot is empty does it cancel. Either way it then forgets the request
  and drops the InstantInteractionCallback, which frees it. So a new id 4 load
  made while the old set was still held (39291 does not clear the slot before
  it asks) is left registered with a freed listener, and the job thread calls
  into freed memory when the file arrives. Measured from the game's own code
  in a crash snapshot of the 1.6.1170 executable; the full reading is on
  Thornswood #6884.

  The fix is the step the game leaves out: on the delivered-set branch, cancel
  the queued request too, exactly as the empty-slot branch does, before the
  callback is dropped. The cancel (63971) finds a request by its address in
  the manager's three lists and touches nothing when it is not there, so a
  request that already finished is safe to cancel. It runs under the same
  process lock as the game's own cancel, so it takes the locks in the order
  the game already does.

  This header holds what the native install and its test both use: the
  window of game bytes the patch is sure of, where the natives the window
  calls are, the bytes that replace the branch, and the helper they call.
*/
namespace InteractionLoadPatch {
using Bytes = std::vector<std::uint8_t>;

#ifdef _WIN32
#define THORNSWOOD_NATIVE_ABI
#else
#define THORNSWOOD_NATIVE_ABI __attribute__((ms_abi))
#endif

using ClearSet = void(THORNSWOOD_NATIVE_ABI*)(void* set, bool free);
using Cancel = void(THORNSWOOD_NATIVE_ABI*)(void* manager, void* request);

// 39798+0x10A to +0x13B on 1.6.1170, as the game has it: with X in RDX,
//   cmp dword [rdx+0x98], 0     slot 4's delivered set count
//   je  cancel
//   lea rcx, [rdx+0x88]        <- Site: the branch this patch replaces
//   mov dl, 1
//   call 38275                  clear the delivered set
//   jmp tail                    and skip the cancel
// cancel:
//   mov rdx, [rdx+0x80]        the queued id 4 request
//   test rdx, rdx
//   je  tail
//   mov rcx, [rip+...]          the animation file manager
//   call 63971                  cancel it
// tail:                         forget the request, drop the callback
constexpr std::size_t kWindowOffset = 0x10A;
constexpr std::array<std::uint8_t, 0x31> kWindow{
  0x83, 0xba, 0x98, 0x00, 0x00, 0x00, 0x00,
  0x74, 0x10,
  0x48, 0x8d, 0x8a, 0x88, 0x00, 0x00, 0x00,
  0xb2, 0x01,
  0xe8, 0xef, 0xd6, 0xf9, 0xff,
  0xeb, 0x18,
  0x48, 0x8b, 0x92, 0x80, 0x00, 0x00, 0x00,
  0x48, 0x85, 0xd2,
  0x74, 0x0c,
  0x48, 0x8b, 0x0d, 0xda, 0xb9, 0xa2, 0x02,
  0xe8, 0xc5, 0xd9, 0x4b, 0x00
};
// From the window's start.
constexpr std::size_t kSite = 9;        // the lea that begins the branch
constexpr std::size_t kSiteSize = 16;   // through the jmp past the cancel
constexpr std::size_t kClearCall = 18;  // e8 rel32 to 38275
constexpr std::size_t kManagerLoad = 37; // 48 8b 0d rel32
constexpr std::size_t kCancelCall = 44; // e8 rel32 to 63971
constexpr std::size_t kTail = 0x31;     // where both branches meet

inline bool Matches(const std::uint8_t* window)
{
  return std::memcmp(window, kWindow.data(), kWindow.size()) == 0;
}

inline std::uintptr_t Relative(std::uintptr_t window, std::size_t at,
                               std::size_t displacement, std::size_t size)
{
  std::int32_t relative;
  std::memcpy(&relative, reinterpret_cast<const void*>(window + at + displacement),
              sizeof(relative));
  return window + at + size + std::intptr_t(relative);
}

struct Natives
{
  ClearSet clearSet = nullptr;  // 38275
  Cancel cancel = nullptr;      // 63971
  void** manager = nullptr;     // the AnimationFileManagerSingleton pointer
};

// What the window itself calls and reads, so the helper calls the very same.
inline Natives Resolve(std::uintptr_t window)
{
  Natives n;
  n.clearSet = reinterpret_cast<ClearSet>(Relative(window, kClearCall, 1, 5));
  n.manager = reinterpret_cast<void**>(Relative(window, kManagerLoad, 3, 7));
  n.cancel = reinterpret_cast<Cancel>(Relative(window, kCancelCall, 1, 5));
  return n;
}

inline Natives g_natives;

// Called where the game cleared slot 4's delivered set, with X in RCX and the
// process lock held: the game's own clear, then the cancel it left out.
inline void THORNSWOOD_NATIVE_ABI EndInteractionLoad(std::uint8_t* slots)
{
  g_natives.clearSet(slots + 0x88, true);
  void* request = *reinterpret_cast<void**>(slots + 0x80);
  if (request && *g_natives.manager) {
    g_natives.cancel(*g_natives.manager, request);
  }
}

// The 16 bytes at the site: mov rcx, rdx; call stub; jmp tail; nops. The stub
// is a jump to EndInteractionLoad within reach of a rel32 call.
inline Bytes Replacement(std::uintptr_t window, std::uintptr_t stub)
{
  const std::uintptr_t site = window + kSite;
  const auto distance = std::int64_t(stub) - std::int64_t(site + 3 + 5);
  if (distance < std::numeric_limits<std::int32_t>::min() ||
      distance > std::numeric_limits<std::int32_t>::max()) {
    throw std::runtime_error("interaction load stub is outside call range");
  }
  Bytes out{ 0x48, 0x8b, 0xca, 0xe8 };
  const auto relative = std::int32_t(distance);
  for (unsigned i = 0; i < 4; ++i) {
    out.push_back(std::uint8_t(std::uint32_t(relative) >> (8 * i)));
  }
  const std::uintptr_t afterJump = site + 3 + 5 + 2;
  out.push_back(0xeb);
  out.push_back(std::uint8_t(window + kTail - afterJump));
  out.resize(kSiteSize, 0x90);
  return out;
}

inline Bytes Stub(std::uintptr_t target)
{
  Bytes out{ 0xff, 0x25, 0, 0, 0, 0 };
  for (unsigned i = 0; i < 8; ++i) {
    out.push_back(std::uint8_t(std::uint64_t(target) >> (8 * i)));
  }
  return out;
}
}
