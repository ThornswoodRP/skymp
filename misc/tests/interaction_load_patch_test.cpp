// Thornswood #6884: the patch that cancels a furniture animation load the
// game leaves queued with a freed listener. See
// skyrim-platform/src/platform_lib/InteractionLoadPatch.h.
//
// This runs the game's own window of 39798 (its bytes as the 1.6.1170
// executable has them, with only the three rel32 targets pointed at stand-ins
// on the same private page) before and after the patch, so what is checked is
// the very instruction stream the game would run, not its spelling. No game,
// DLL, user data or game assets are involved.
#include "InteractionLoadPatch.h"
#include <cstdio>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <sys/mman.h>
#endif

namespace P = InteractionLoadPatch;
int failed = 0;
#define CHECK(x) do { if (!(x)) { ++failed; std::printf("FAIL line %d: %s\n", __LINE__, #x); } else { std::printf("PASS %s\n", #x); } } while (0)

class Code
{
public:
  Code()
  {
#ifdef _WIN32
    memory = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
#else
    auto* ptr = mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    memory = ptr == MAP_FAILED ? nullptr : static_cast<std::uint8_t*>(ptr);
#endif
    if (!memory) throw std::runtime_error("cannot allocate native fixture");
  }
  ~Code()
  {
#ifdef _WIN32
    VirtualFree(memory, 0, MEM_RELEASE);
#else
    munmap(memory, 4096);
#endif
  }
  std::uintptr_t At(std::size_t n) { return reinterpret_cast<std::uintptr_t>(memory + n); }
  void Put(std::size_t at, const P::Bytes& bytes)
  {
    if (at + bytes.size() > 4096) throw std::runtime_error("native fixture is too large");
    std::memcpy(memory + at, bytes.data(), bytes.size());
  }
  void PutRelative(std::size_t at, std::uintptr_t target, std::size_t next)
  {
    const auto relative = std::int32_t(std::int64_t(target) - std::int64_t(At(next)));
    std::memcpy(memory + at, &relative, sizeof(relative));
  }
  void Run()
  {
#ifdef _WIN32
    DWORD previous;
    if (!VirtualProtect(memory, 4096, PAGE_EXECUTE_READ, &previous))
      throw std::runtime_error("cannot execute native fixture");
    FlushInstructionCache(GetCurrentProcess(), memory, 4096);
#else
    if (mprotect(memory, 4096, PROT_READ | PROT_EXEC)) throw std::runtime_error("cannot execute native fixture");
#endif
    reinterpret_cast<void (*)()>(memory)();
  }
private:
  std::uint8_t* memory;
};

// What the game's process keeps for its animation sets: slot 4's queued
// request at +0x80, its delivered set at +0x88, that set's count at +0x98.
struct alignas(16) Slots { std::uint8_t bytes[0x100]{}; };
std::uint32_t& Count(Slots& s) { return *reinterpret_cast<std::uint32_t*>(s.bytes + 0x98); }
void*& Request(Slots& s) { return *reinterpret_cast<void**>(s.bytes + 0x80); }

int clears = 0, cancels = 0;
void* clearedSet = nullptr;
void* cancelledManager = nullptr;
void* cancelledRequest = nullptr;
int manager = 0;
int request = 0;

void THORNSWOOD_NATIVE_ABI FakeClear(void* set, bool free)
{
  ++clears;
  clearedSet = set;
  if (free) *reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(set) + 0x10) = 0;
}
void THORNSWOOD_NATIVE_ABI FakeCancel(void* m, void* r)
{
  ++cancels;
  cancelledManager = m;
  cancelledRequest = r;
}

struct Outcome { int clears, cancels; void* set; void* manager; void* request; };

Outcome RunWindow(bool patched, std::uint32_t count, void* queued)
{
  Code code;
  Slots slots;
  Count(slots) = count;
  Request(slots) = queued;
  clears = cancels = 0;
  clearedSet = cancelledManager = cancelledRequest = nullptr;

  constexpr std::size_t window = 0x20, stubs = 0x200, cell = 0x300;
  // A native function's frame: aligned RSP with its 32 bytes of shadow space,
  // and X in RDX, as 39798 has them when it reaches the window.
  P::Bytes entry{ 0x48, 0x83, 0xec, 0x28, 0x48, 0xba };
  for (unsigned i = 0; i < 8; ++i) entry.push_back(std::uint8_t(reinterpret_cast<std::uintptr_t>(&slots) >> (8 * i)));
  P::Bytes toWindow{ 0xe9, 0, 0, 0, 0 };
  code.Put(0, entry);
  code.Put(entry.size(), toWindow);
  code.PutRelative(entry.size() + 1, code.At(window), entry.size() + 5);

  code.Put(window, P::Bytes(P::kWindow.begin(), P::kWindow.end()));
  code.Put(window + P::kTail, P::Bytes{ 0x48, 0x83, 0xc4, 0x28, 0xc3 });
  code.Put(stubs, P::Stub(reinterpret_cast<std::uintptr_t>(&FakeClear)));
  code.Put(stubs + 0x10, P::Stub(reinterpret_cast<std::uintptr_t>(&FakeCancel)));
  code.Put(stubs + 0x20, P::Stub(reinterpret_cast<std::uintptr_t>(&P::EndInteractionLoad)));
  void* managerPointer = &manager;
  code.Put(cell, P::Bytes(reinterpret_cast<std::uint8_t*>(&managerPointer),
                          reinterpret_cast<std::uint8_t*>(&managerPointer) + sizeof(managerPointer)));
  code.PutRelative(window + P::kClearCall + 1, code.At(stubs), window + P::kClearCall + 5);
  code.PutRelative(window + P::kManagerLoad + 3, code.At(cell), window + P::kManagerLoad + 7);
  code.PutRelative(window + P::kCancelCall + 1, code.At(stubs + 0x10), window + P::kCancelCall + 5);

  const auto natives = P::Resolve(code.At(window));
  CHECK(reinterpret_cast<std::uintptr_t>(natives.clearSet) == code.At(stubs));
  CHECK(reinterpret_cast<std::uintptr_t>(natives.cancel) == code.At(stubs + 0x10));
  CHECK(reinterpret_cast<std::uintptr_t>(natives.manager) == code.At(cell));
  P::g_natives = { &FakeClear, &FakeCancel, natives.manager };

  if (patched) {
    code.Put(window + P::kSite, P::Replacement(code.At(window), code.At(stubs + 0x20)));
  }
  code.Run();
  return { clears, cancels, clearedSet, cancelledManager, cancelledRequest };
}

int main()
{
  // The window is the game's: every branch lands where the listing says.
  CHECK(P::kWindow[P::kSite] == 0x48 && P::kWindow[P::kSite + 15] == 0x18);
  CHECK(7 + 2 + P::kWindow[8] == 25);                 // je to the cancel
  CHECK(P::kSite + 16 + P::kWindow[P::kSite + 15] == P::kTail); // jmp past it
  CHECK(35 + 2 + P::kWindow[36] == P::kTail);         // je past a null request

  // Its rel32 targets, read at the game's own addresses, are the natives the
  // install checks against the Address Library: 38275 at 0x6AAB90, the manager
  // pointer at 0x3138E90 and 63971 at 0xBCAE80, with the window at 0x70D48A.
  {
    const auto window = reinterpret_cast<std::uintptr_t>(P::kWindow.data());
    const auto base = window - 0x70D48A;
    const auto n = P::Resolve(window);
    CHECK(reinterpret_cast<std::uintptr_t>(n.clearSet) - base == 0x6AAB90);
    CHECK(reinterpret_cast<std::uintptr_t>(n.manager) - base == 0x3138E90);
    CHECK(reinterpret_cast<std::uintptr_t>(n.cancel) - base == 0xBCAE80);
  }
  CHECK(P::Matches(P::kWindow.data()));
  {
    auto changed = P::kWindow;
    changed[P::kSite + 15] = 0x00;
    CHECK(!P::Matches(changed.data()));
  }

  // The crash's state, as the game runs it: a delivered set in slot 4 and a
  // queued id 4 load. The set is cleared and the load is never cancelled.
  {
    const auto o = RunWindow(false, 3, &request);
    CHECK(o.clears == 1);
    CHECK(o.cancels == 0);
  }
  // Patched: the same set is cleared, and the queued load is cancelled with
  // the manager the game's own cancel uses.
  {
    const auto o = RunWindow(true, 3, &request);
    CHECK(o.clears == 1);
    CHECK(o.cancels == 1);
    CHECK(o.manager == &manager);
    CHECK(o.request == &request);
  }
  // Patched, the empty slot runs the game's own cancel exactly as before.
  {
    const auto o = RunWindow(true, 0, &request);
    CHECK(o.clears == 0);
    CHECK(o.cancels == 1);
    CHECK(o.request == &request);
  }
  // Patched, a delivered set with nothing queued clears and cancels nothing.
  {
    const auto o = RunWindow(true, 2, nullptr);
    CHECK(o.clears == 1);
    CHECK(o.cancels == 0);
  }
  std::printf(failed ? "FAILED %d\n" : "all passed\n", failed);
  return failed ? 1 : 0;
}
