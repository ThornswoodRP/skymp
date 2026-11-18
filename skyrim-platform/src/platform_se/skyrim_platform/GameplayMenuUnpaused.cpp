#include "GameplayMenuUnpaused.h"
#include "FridaHookHandler.h"
#include "GameplayMenuPatches.h"
#include "GameplayMenuPolicy.h"
#include <array>
#include <atomic>
#include <map>

// UI job synchronization follows Vermunds' Skyrim Souls RE, 1-6-1170,
// src/MenuProcessing.cpp (GPL-3.0-or-later). Its barrier is retained; here one
// owner also replaces vanilla's paused/title passes, so all message processing
// deliberately happens AFTER rendering. No Skyrim Souls controls/cursor or
// time-scale changes are installed. See docs/GameplayMenus.md for native proof.
namespace {
namespace P = GameplayMenuPatches;
using UiPass = void (*)(RE::UI*);
UiPass g_messages = nullptr, g_movies = nullptr;
void* (*g_console)() = nullptr;
void (*g_commands)(void*) = nullptr;
void (*g_barrier)(void*) = nullptr;
std::atomic_bool g_requested{ false }, g_entered{ false }, g_installed{ false };
std::atomic<std::uint32_t> g_enabled{ 0 };
static_assert(decltype(g_enabled)::is_always_lock_free && sizeof(g_enabled) == 4);
GameplayMenuPolicy::Session g_session; // main-thread owner only
thread_local bool g_insideOwner = false;
std::array<std::atomic<std::uint32_t>, 3> g_passes{};
std::atomic<std::uint32_t> g_outside{ 0 };
SKSE::Trampoline g_trampoline("Gameplay menus");

struct Observers
{
  GumInterceptor* interceptor = nullptr;
  std::array<GumInvocationListener*, 3> listeners{};
  void Clear()
  {
    if (!interceptor) return;
    gum_interceptor_begin_transaction(interceptor);
    for (auto* listener : listeners) if (listener) gum_interceptor_detach(interceptor, listener);
    gum_interceptor_end_transaction(interceptor);
    for (auto*& listener : listeners) {
      if (listener) g_object_unref(listener);
      listener = nullptr;
    }
    g_object_unref(interceptor);
    interceptor = nullptr;
  }
  ~Observers() { Clear(); }
  bool Install(const std::array<std::uintptr_t, 3>& addresses)
  {
    Frida::HookHandler::GetSingleton(); // the existing owner initializes Gum
    interceptor = gum_interceptor_obtain();
    for (std::size_t i = 0; i < listeners.size(); ++i) {
      listeners[i] = gum_make_call_listener([](GumInvocationContext*, gpointer counter) {
        if (!g_installed.load(std::memory_order_relaxed)) return;
        static_cast<std::atomic<std::uint32_t>*>(counter)->fetch_add(1, std::memory_order_relaxed);
        if (!g_insideOwner) g_outside.fetch_add(1, std::memory_order_relaxed);
      }, nullptr, &g_passes[i], nullptr);
      if (!listeners[i] || gum_interceptor_attach(interceptor,
          reinterpret_cast<void*>(addresses[i]), listeners[i], nullptr) != GUM_ATTACH_OK) {
        Clear();
        return false;
      }
    }
    return true;
  }
} g_observers;

void UpdateEligibility(RE::UI* ui)
{
  const auto* player = RE::PlayerCharacter::GetSingleton();
  const bool world = g_entered.load(std::memory_order_acquire) && player &&
    player->Is3DLoaded() && !ui->IsMenuOpen(RE::MainMenu::MENU_NAME);
  g_enabled.store(g_session.Update(g_requested.load(std::memory_order_acquire), world),
                  std::memory_order_release);
}

RE::UI_MESSAGE_RESULTS Show(RE::IMenu* menu, RE::UIMessage* message)
{
  if (message->menu == RE::MainMenu::MENU_NAME) {
    // A returned title can retain a player/cell/3D. This explicit transition,
    // unlike a generic modal or connection loss, ends the live-world lifetime.
    g_entered.store(false, std::memory_order_release);
    g_requested.store(false, std::memory_order_release);
    g_enabled.store(0, std::memory_order_release);
    g_session.Update(false, false);
  }
  return GameplayMenuPolicy::Show(g_enabled.load(std::memory_order_acquire) != 0,
    [&] { return menu->ProcessMessage(*message); },
    [&] { return menu->menuFlags.underlying(); },
    [&](std::uint32_t flags) { menu->menuFlags = static_cast<RE::IMenu::Flag>(flags); });
}

void Owner(void* heap)
{
  g_barrier(heap); // original ScrapHeap barrier, once, before UI access
  auto* ui = RE::UI::GetSingleton();
  UpdateEligibility(ui);
  g_insideOwner = true;
  g_messages(ui);
  g_movies(ui);
  g_commands(g_console());
  g_insideOwner = false;
  static std::uint64_t frames = 0, bad = 0;
  static std::array<std::uint64_t, 3> totals{};
  bool mismatch = false;
  for (std::size_t i = 0; i < totals.size(); ++i) {
    const auto n = g_passes[i].exchange(0, std::memory_order_relaxed);
    totals[i] += n;
    mismatch |= n != 1;
  }
  ++frames;
  bad += mismatch;
  if (frames == 1 || frames % 1200 == 0 || (mismatch && bad <= 5)) {
    logger::info("menu UI owner: frames {} messages {} movies {} console {} mismatches {} outside {} live {} pauses {}",
      frames, totals[0], totals[1], totals[2], bad, g_outside.load(),
      g_enabled.load(), ui->numPausesGame);
  }
}

struct Patch
{
  std::uintptr_t address;
  P::Bytes before, after;
};

bool Matches(const std::vector<Patch>& patches)
{
  for (const auto& p : patches) {
    if (p.before.size() != p.after.size() ||
        std::memcmp(reinterpret_cast<void*>(p.address), p.before.data(), p.before.size())) {
      logger::error("gameplay menus: native bytes differ at {:X}; no menu patch installed",
                    p.address - REL::Module::get().base());
      return false;
    }
  }
  return true;
}

bool Commit(const std::vector<Patch>& patches)
{
  if (!Matches(patches)) return false;
  SYSTEM_INFO info{};
  GetSystemInfo(&info);
  std::map<std::uintptr_t, DWORD> pages;
  for (const auto& p : patches) {
    const auto first = p.address / info.dwPageSize * info.dwPageSize;
    const auto last = (p.address + p.before.size() - 1) / info.dwPageSize * info.dwPageSize;
    for (auto page = first; page <= last; page += info.dwPageSize) pages.emplace(page, 0);
  }
  auto restore = [&] {
    for (const auto& [page, old] : pages) if (old) {
      DWORD ignored;
      if (!VirtualProtect(reinterpret_cast<void*>(page), info.dwPageSize, old, &ignored))
        logger::critical("gameplay menus: cannot restore code page protection: {}", GetLastError());
    }
  };
  // Obtain every writable page before touching any instruction. Any failure
  // leaves ALL eight original sites intact, not half a scheduling change.
  for (auto& [page, old] : pages) {
    if (!VirtualProtect(reinterpret_cast<void*>(page), info.dwPageSize, PAGE_EXECUTE_READWRITE, &old)) {
      logger::error("gameplay menus: cannot prepare code pages: {}; no patch installed", GetLastError());
      restore();
      return false;
    }
  }
  if (!Matches(patches)) { restore(); return false; }
  for (const auto& p : patches)
    std::memcpy(reinterpret_cast<void*>(p.address), p.after.data(), p.after.size());
  FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
  restore();
  return true;
}

std::uintptr_t Store(const P::Bytes& bytes)
{
  auto* memory = g_trampoline.allocate(bytes.size());
  std::memcpy(memory, bytes.data(), bytes.size());
  return reinterpret_cast<std::uintptr_t>(memory);
}
}

void GameplayMenuUnpaused::RequestClient(bool requested)
{
  g_requested.store(requested, std::memory_order_release);
}
void GameplayMenuUnpaused::WorldLoading()
{
  g_entered.store(false, std::memory_order_release);
  g_enabled.store(0, std::memory_order_release);
}
void GameplayMenuUnpaused::WorldLoaded(bool success)
{
  g_entered.store(success, std::memory_order_release);
  if (!success) g_enabled.store(0, std::memory_order_release);
}

bool GameplayMenuUnpaused::Install()
{
  if (g_installed.load()) return true;
  if (REL::Module::get().version() != REL::Version(1, 6, 1170, 0)) {
    logger::error("gameplay menus: unsupported runtime; no native menu patch installed");
    return false;
  }
  const auto main = REL::ID(36564).address();
  const auto job = REL::ID(39042).address();
  const auto show = REL::ID(82082).address() + 0x6ba;
  const auto map = REL::ID(53112).address() + 0x95;
  const auto stats = REL::ID(52510).address() + 0xa10;
  g_messages = reinterpret_cast<UiPass>(REL::ID(82082).address());
  g_movies = reinterpret_cast<UiPass>(REL::ID(82083).address());
  g_console = reinterpret_cast<decltype(g_console)>(REL::ID(52950).address());
  g_commands = reinterpret_cast<decltype(g_commands)>(REL::ID(52952).address());
  const auto barrier = main + 0xadf;
  g_barrier = reinterpret_cast<decltype(g_barrier)>(barrier + 5 +
    *reinterpret_cast<std::int32_t*>(barrier + 1));
  std::vector<Patch> patches{
    { job + 0xb, { 0x75, 0x25 }, { 0xeb, 0x25 } },
    { main + 0xa8a, { 0xe8, 0x71, 0xd6, 0x95, 0 }, P::Bytes(5, 0x90) },
    { main + 0xaf0, { 0xe8, 0xfb, 0xe2, 0x95, 0 }, P::Bytes(5, 0x90) },
    { main + 0xaf5, { 0xe8, 0x46, 0x49, 0x33, 0, 0x48, 0x8b, 0xc8,
                     0xe8, 0xee, 0x4a, 0x33, 0 }, P::Bytes(13, 0x90) },
    { barrier, { 0xe8, 0xec, 0xf2, 0x67, 0 }, P::Bytes(5, 0x90) },
    { show, { 0xff, 0x50, 0x20, 0x83, 0xf8, 1 }, P::Bytes(6, 0x90) },
    { map, { 0x83, 0xc8, 0x20, 0xeb, 3 }, P::Bytes(5, 0x90) },
    { stats, { 0x83, 0x4e, 0x1c, 0x20, 0x48, 0x83, 0xbe, 0xa8, 2, 0, 0, 0 }, P::Bytes(12, 0x90) }
  };
  if (!Matches(patches)) return false;
  // Prepare all code and observers before changing any functional instruction.
  g_trampoline.create(1024);
  P::Bytes owner;
  P::Jump(owner, reinterpret_cast<std::uintptr_t>(&Owner));
  patches[4].after = P::Branch(0xe8, barrier, Store(owner), 5);
  patches[5].after = P::Branch(0xe9, show,
    Store(P::Show(reinterpret_cast<std::uintptr_t>(&Show), show + 6)), 6);
  const auto enabled = reinterpret_cast<std::uintptr_t>(&g_enabled);
  patches[6].after = P::Branch(0xe9, map, Store(P::Background(enabled, map + 8, false)), 5);
  patches[7].after = P::Branch(0xe9, stats, Store(P::Background(enabled, stats + 12, true)), 12);
  if (!g_observers.Install({ reinterpret_cast<std::uintptr_t>(g_messages),
      reinterpret_cast<std::uintptr_t>(g_movies), reinterpret_cast<std::uintptr_t>(g_commands) })) {
    logger::error("gameplay menus: cannot observe native UI passes; no patch installed");
    return false;
  }
  if (!Commit(patches)) { g_observers.Clear(); return false; }
  g_installed.store(true, std::memory_order_release);
  logger::info("gameplay menus: guarded AE hooks installed; one main-thread UI pass after rendering");
  return true;
}

bool GameplayMenuUnpaused::Installed()
{
  return g_installed.load(std::memory_order_acquire);
}
