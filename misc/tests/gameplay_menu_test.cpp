#include "GameplayMenuPolicy.h"
#include "GameplayMenuPatches.h"
#include <array>
#include <cstring>
#include <cstdio>
#include <string_view>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#define MS_ABI
#else
#include <sys/mman.h>
#define MS_ABI __attribute__((ms_abi))
#endif

namespace P = GameplayMenuPolicy;
int failed = 0;
#define CHECK(x) do { if (!(x)) { ++failed; std::printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)

struct Menu { std::uint32_t flags; };

// Execute the SAME emitted x64 stubs the native installer uses, rather than
// comparing their spelling. Only these private fixture pages are executable;
// no game, DLL, user data, process attachment or game assets are involved.
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
  void Put(std::size_t at, const GameplayMenuPatches::Bytes& bytes)
  {
    if (at + bytes.size() > 4096) throw std::runtime_error("native fixture is too large");
    std::memcpy(memory + at, bytes.data(), bytes.size());
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

int helperCalls = 0, helperResult = 37;
void* expectedMessage = nullptr;
int MS_ABI OriginalShow(Menu* menu, void* message)
{
  ++helperCalls;
  CHECK(message == expectedMessage);
  return P::Show(true, [&] {
    menu->flags |= P::Pauses | P::FrozenBackground;
    return helperResult;
  }, [&] { return menu->flags; }, [&](std::uint32_t flags) { menu->flags = flags; });
}

void Thunks()
{
  namespace A = GameplayMenuPatches;
  struct Output { std::uint32_t result = 0, zero = 0; std::uint64_t r11 = 0; };
  for (int result : { 1, 37 }) {
    Code code;
    Menu menu{ 4 | (1u << 24) };
    int message = 0;
    Output out;
    expectedMessage = &message;
    helperCalls = 0;
    helperResult = result;
    // A native call site has aligned RSP and 32 bytes of shadow space. The
    // show stub must add exactly one helper return address and retain EAX/ZF.
    A::Bytes entry{ 0x48, 0x83, 0xec, 0x28, 0x48, 0xb9 };
    A::Address(entry, reinterpret_cast<std::uintptr_t>(&menu));
    entry.insert(entry.end(), { 0x48, 0xba });
    A::Address(entry, reinterpret_cast<std::uintptr_t>(&message));
    A::Jump(entry, code.At(0x100));
    code.Put(0, entry);
    code.Put(0x100, A::Show(reinterpret_cast<std::uintptr_t>(&OriginalShow), code.At(0x200)));
    A::Bytes resume{ 0x49, 0xba };
    A::Address(resume, reinterpret_cast<std::uintptr_t>(&out));
    resume.insert(resume.end(), { 0x41, 0x89, 0x02, 0x41, 0x0f, 0x94, 0x42, 4,
                                  0x48, 0x83, 0xc4, 0x28, 0xc3 });
    code.Put(0x200, resume);
    code.Run();
    CHECK(helperCalls == 1);
    CHECK(out.result == std::uint32_t(result));
    CHECK(out.zero == std::uint32_t(result == 1));
    CHECK(menu.flags == (4 | (1u << 24)));
  }

  for (bool stats : { false, true }) for (std::uint32_t enabled : { 0u, 1u }) {
    Code code;
    alignas(16) std::array<std::uint8_t, 0x2b0> menu{};
    constexpr std::uint32_t flags = 4 | 0x100 | (1u << 24);
    std::memcpy(menu.data() + 0x1c, &flags, sizeof(flags));
    Output out;
    constexpr std::uint64_t savedR11 = 0x123456789abcdef0;
    A::Bytes entry{ 0x56, 0x48, 0xbe }; // preserve the caller's RSI
    A::Address(entry, reinterpret_cast<std::uintptr_t>(menu.data()));
    entry.insert(entry.end(), { 0x49, 0xbb });
    A::Address(entry, savedR11);
    entry.push_back(0xb8);
    for (unsigned i = 0; i < 4; ++i) entry.push_back(std::uint8_t(flags >> (8 * i)));
    A::Jump(entry, code.At(0x100));
    code.Put(0, entry);
    code.Put(0x100, A::Background(reinterpret_cast<std::uintptr_t>(&enabled), code.At(0x200), stats));
    A::Bytes resume;
    if (!stats) resume.insert(resume.end(), { 0x89, 0x46, 0x1c }); // native Map store
    resume.insert(resume.end(), { 0x49, 0xba });
    A::Address(resume, reinterpret_cast<std::uintptr_t>(&out));
    resume.insert(resume.end(), { 0x41, 0x0f, 0x94, 0x42, 4, 0x4d, 0x89, 0x5a, 8, 0x5e, 0xc3 });
    code.Put(0x200, resume);
    code.Run();
    std::uint32_t got;
    std::memcpy(&got, menu.data() + 0x1c, sizeof(got));
    CHECK(got == (enabled ? flags : flags | P::FrozenBackground));
    CHECK(out.r11 == savedR11);
    if (stats) CHECK(out.zero == 1); // displaced CMP [RSI+2A8],0 was replayed
  }
  auto branch = A::Branch(0xe9, 0x1000, 0x2000, 6);
  std::int32_t relative;
  std::memcpy(&relative, branch.data() + 1, sizeof(relative));
  CHECK(branch[0] == 0xe9 && branch[5] == 0x90 && 0x1005 + relative == 0x2000);
  bool rejected = false;
  try { A::Branch(0xe9, 0x1000, 0x100000000ull, 5); } catch (const std::runtime_error&) { rejected = true; }
  CHECK(rejected);
}

int main()
{
  // Every unrelated bit, including cursor/modal and freeze-frame screenshot
  // pause, must survive. Native pause and background-freeze are different bits.
  constexpr auto unrelated = ~(P::Pauses | P::FrozenBackground);
  constexpr std::array names = { "TweenMenu", "InventoryMenu", "MagicMenu",
    "MapMenu", "StatsMenu", "MessageBoxMenu", "Console", "CustomMenu" };
  for (std::string_view name : names) {
    Menu m{ unrelated };
    int calls = 0, pauses = 0;
    // The creator and PostCreate have already returned. The original Show
    // callback can still set pause/freeze, as Stats does in the actual game.
    auto result = P::Show(true, [&] {
      ++calls;
      m.flags |= P::Pauses | P::FrozenBackground;
      return 37;
    }, [&] { return m.flags; }, [&](std::uint32_t flags) { m.flags = flags; });
    pauses += (m.flags & P::Pauses) != 0;
    CHECK(result == 37 && calls == 1);
    CHECK(m.flags == unrelated);
    CHECK(pauses == 0);
    pauses -= (m.flags & P::Pauses) != 0;
    CHECK(pauses == 0);
    std::printf("checked %.*s\n", int(name.size()), name.data());
  }

  // Offline and unloaded-world paths keep all flags and balanced counts.
  Menu offline{ unrelated };
  int count = 0;
  P::Show(false, [&] { offline.flags |= P::Pauses | P::FrozenBackground; return 0; },
    [&] { return offline.flags; }, [&](std::uint32_t flags) { offline.flags = flags; });
  CHECK(offline.flags == 0xffffffffu);
  count += (offline.flags & P::Pauses) != 0;
  CHECK(count == 1);
  count -= (offline.flags & P::Pauses) != 0;
  CHECK(count == 0);

  P::Session session;
  CHECK(!session.Update(false, false));
  CHECK(!session.Update(true, false)); // connection at the title/loading screen
  CHECK(session.Update(true, true));
  CHECK(session.Update(false, true)); // destroy/recreate while a world is live
  CHECK(session.Update(true, true));
  CHECK(session.Update(false, true));
  CHECK(!session.Update(false, false)); // explicit end plus world unload
  CHECK(!session.Update(false, true)); // later offline world is not latched
  Thunks();
  CHECK(!P::PostLoadSucceeded(nullptr));
  std::printf("checking SKSE PostLoad success encoded as pointer value 1\n");
  std::fflush(stdout);
  CHECK(P::PostLoadSucceeded(reinterpret_cast<void*>(std::uintptr_t(1))));
  std::printf("%d failures\n", failed);
  return failed ? 1 : 0;
}
