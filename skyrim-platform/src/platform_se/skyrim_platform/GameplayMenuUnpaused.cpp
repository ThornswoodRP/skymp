#include "GameplayMenuUnpaused.h"
#include "BrowserApi.h"
#include "FridaHookHandler.h"
#include "GameplayMenuPatches.h"
#include "GameplayMenuPolicy.h"
#include "InventoryShortcut.h"
#include "PageFocusMenu.h"
#include "SleepWaitChoice.h"
#include <array>
#include <atomic>
#include <map>

// UI job synchronization follows Vermunds' Skyrim Souls RE, 1-6-1170,
// src/MenuProcessing.cpp (GPL-3.0-or-later). Its after-render phase is retained;
// the displaced original heap call is preserved, not treated as a job wait. One
// owner also replaces vanilla's paused/title passes, so all message processing
// deliberately happens AFTER rendering. No Skyrim Souls controls/cursor or
// time-scale changes are installed. See docs/GameplayMenus.md for native proof.
namespace {
namespace P = GameplayMenuPatches;
using UiPass = void (*)(RE::UI*);
UiPass g_messages = nullptr, g_movies = nullptr;
void* (*g_console)() = nullptr;
void (*g_commands)(void*) = nullptr;
void (*g_originalHeapCall)(void*) = nullptr;
std::atomic_bool g_installed{ false };
std::atomic<std::uint32_t> g_enabled{ 0 };
std::atomic<std::uint32_t> g_inventoryLifetime{ 0 };
std::atomic<std::uint32_t> g_inventoryReady{ 0 };
std::atomic<std::uint32_t> g_inventoryRequest{ 0 };
static_assert(decltype(g_enabled)::is_always_lock_free && sizeof(g_enabled) == 4);
GameplayMenuPolicy::World g_world;
thread_local bool g_insideOwner = false;
std::array<std::atomic<std::uint32_t>, 3> g_passes{};
std::atomic<std::uint32_t> g_outside{ 0 };
SKSE::Trampoline g_trampoline("Gameplay menus");
SleepWaitChoice<RE::FxDelegateArgs> g_sleepChoice;
SleepWaitHandoff g_sleepHandoff;
using SleepCallback = RE::FxDelegateHandler::CallbackFn;
SleepCallback* g_sleepOk = nullptr;
SleepCallback* g_sleepCancel = nullptr;

bool SleepOkVerified()
{
  constexpr std::array<std::uint8_t, 10> bytes{
    0x48, 0x89, 0x5c, 0x24, 8, 0x57, 0x48, 0x83, 0xec, 0x30 };
  return std::memcmp(reinterpret_cast<void*>(g_sleepOk), bytes.data(), bytes.size()) == 0;
}
bool SleepCancelVerified()
{
  // Native Cancel only queues kHide. It never reads the supplied arguments
  // or requests sleep/wait, changes hours, pause flags, or Main.freezeTime.
  constexpr std::array<std::uint8_t, 33> bytes{
    0x48, 0x8b, 0x15, 0xb1, 0xa8, 0x79, 1, 0x45, 0x33, 0xc9,
    0x48, 0x8b, 0x0d, 0x9f, 0xa8, 0x79, 1, 0x48, 0x81, 0xc2,
    0x38, 1, 0, 0, 0x45, 0x8d, 0x41, 3, 0xe9, 0x9f, 0x11, 0x85, 0xff };
  return std::memcmp(reinterpret_cast<void*>(g_sleepCancel), bytes.data(), bytes.size()) == 0;
}
void HideSleepChoice()
{
  RE::UIMessageQueue::GetSingleton()->AddMessage(RE::SleepWaitMenu::MENU_NAME,
    RE::UI_MESSAGE_TYPE::kHide, nullptr);
}
void ExplainRealTime()
{
  RE::DebugNotification("Time passes for everyone here. Sleeping and waiting cannot skip hours.");
}
void SleepConfirm(const RE::FxDelegateArgs& args)
{
  const bool live = g_enabled.load(std::memory_order_acquire) != 0;
  auto current = RE::UI::GetSingleton()->GetMenu<RE::SleepWaitMenu>();
  const bool owner = current && args.GetHandler() == current.get();
  if (!g_sleepChoice.Run(args, live, owner && SleepCancelVerified(), HideSleepChoice, ExplainRealTime)) {
    logger::error("sleep/wait: unbound callback refused");
    HideSleepChoice();
    ExplainRealTime();
  }
}

bool PrepareSleepChoice(RE::IMenu* menu, bool live)
{
  auto* callback = menu->fxDelegate ? menu->fxDelegate->callbacks.GetAlt("OK") : nullptr;
  const bool owner = callback && callback->handler.get() == menu;
  if (g_sleepChoice.Bind(callback ? &callback->callback : nullptr,
      g_sleepOk, g_sleepCancel, SleepConfirm, SleepOkVerified() && SleepCancelVerified(), owner))
    return true;
  if (!live) return true; // unsupported offline callback stays exactly as it was
  // Fail before native stack/count insertion: returning kIgnore below makes
  // Skyrim refuse this Show, so an unknown OK callback cannot receive input.
  // Its callback is left untouched; an offline Show still has its original.
  logger::error("sleep/wait: unsupported callback, code or owner; live Show refused");
  HideSleepChoice();
  ExplainRealTime();
  return false;
}

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
  g_enabled.store(g_world.Eligible(player && player->Is3DLoaded(),
                                   ui->IsMenuOpen(RE::MainMenu::MENU_NAME)),
                  std::memory_order_release);
}

bool InventoryFlag(RE::GFxMovieView& movie, const char* path, bool expected)
{
  RE::GFxValue value;
  return movie.GetVariable(&value, path) && value.IsBool() && value.GetBool() == expected;
}

bool InventoryOwnsInput(RE::UI& ui, const RE::IMenu& inventory)
{
  // Cursor and HUD movies can be above Inventory without owning the input.
  // A different input context or a modal above it owns this press instead.
  for (auto i = ui.menuStack.size(); i > 0; --i) {
    const auto& menu = ui.menuStack[i - 1];
    if (menu.get() == &inventory)
      return true;
    if (menu && (menu->Modal() ||
                 menu->inputContext.get() != RE::UserEvents::INPUT_CONTEXT_ID::kNone))
      return false;
  }
  return false;
}

RE::GPtr<RE::InventoryMenu> InventoryTakingInput(RE::UI* ui)
{
  if (!ui || BrowserApi::HasInputFocus() || ui->IsMenuOpen(PageFocusMenu::MENU_NAME))
    return {};
  auto inventory = ui->GetMenu<RE::InventoryMenu>();
  if (inventory && inventory->OnStack() && InventoryOwnsInput(*ui, *inventory))
    return inventory;
  return {};
}

bool InventoryCanClose(RE::GFxMovieView& movie)
{
  return InventoryFlag(movie, "_root.Menu_mc.bFadedIn", true) &&
         InventoryFlag(movie, "_root.Menu_mc._bMenuClosing", false) &&
         InventoryFlag(movie, "_root.Menu_mc.inventoryLists.itemList.disableInput", false) &&
         InventoryFlag(movie, "_root.Menu_mc.inventoryLists.categoryList.disableInput", false);
}

std::uint32_t ReadyInventory(RE::UI* ui)
{
  const auto lifetime = g_inventoryLifetime.load();
  if (!(lifetime & 1u) || !g_enabled.load(std::memory_order_acquire))
    return 0;
  const auto menu = ui->GetMenu<RE::InventoryMenu>();
  return menu && menu->OnStack() && menu->uiMovie && InventoryCanClose(*menu->uiMovie)
    ? lifetime : 0;
}

void CloseRequestedInventory(RE::UI* ui)
{
  const auto request = g_inventoryRequest.exchange(0);
  if (!InventoryShortcut::Current(request, g_inventoryLifetime.load()) ||
      !g_enabled.load(std::memory_order_acquire))
    return;

  const auto* controls = RE::ControlMap::GetSingleton();
  if (!controls || controls->textEntryCount != 0)
    return;

  auto menu = InventoryTakingInput(ui);
  if (!menu || !menu->uiMovie || !menu->fxDelegate)
    return;

  auto& movie = *menu->uiMovie;
  // Check the pinned SkyUI movie after its input pass. If Flash already
  // handled the same press, it is already closing.
  if (!InventoryCanClose(movie))
    return;
  const auto* closeTween = menu->fxDelegate->callbacks.GetAlt("CloseTweenMenu");
  if (!closeTween || !closeTween->callback || closeTween->handler.get() != menu.get())
    return;

  // Same sequence as SkyUI's Tab branch, on this movie. The movie saves
  // selection and delegates CloseMenu itself when the fade completes.
  if (movie.Invoke("_root.Menu_mc.startMenuFade", nullptr, nullptr, 0)) {
    RE::FxDelegateArgs args({}, menu.get(), &movie, nullptr, 0);
    closeTween->callback(args);
  }
}

RE::UI_MESSAGE_RESULTS Show(RE::IMenu* menu, RE::UIMessage* message)
{
  if (message->menu == RE::MainMenu::MENU_NAME) {
    // A returned title can retain a player/cell/3D. This explicit transition,
    // unlike a generic modal or connection loss, ends the live-world lifetime.
    g_world.Title();
    g_enabled.store(0, std::memory_order_release);
  }
  const char* raw = message->menu.c_str();
  const std::string_view name{ raw ? raw : "" };
  return GameplayMenuPolicy::Show(GameplayMenuPolicy::Unpauses(
      g_enabled.load(std::memory_order_acquire) != 0, name),
    [&] {
      const auto result = menu->ProcessMessage(*message);
      if (result != RE::UI_MESSAGE_RESULTS::kIgnore &&
          message->menu == RE::SleepWaitMenu::MENU_NAME &&
          !PrepareSleepChoice(menu, g_enabled.load() != 0))
        return RE::UI_MESSAGE_RESULTS::kIgnore;
      return result;
    },
    [&] { return menu->menuFlags.underlying(); },
    [&](std::uint32_t flags) { menu->menuFlags = static_cast<RE::IMenu::Flag>(flags); });
}

void Owner(void* heap)
{
  g_originalHeapCall(heap); // original keepPagesRequest increment, exactly once
  auto* ui = RE::UI::GetSingleton();
  UpdateEligibility(ui);
  g_insideOwner = true;
  g_sleepHandoff.BeforeMessages(g_enabled.load() != 0,
    ui->IsMenuOpen(RE::SleepWaitMenu::MENU_NAME), [] {
      HideSleepChoice();
      ExplainRealTime();
    });
  g_messages(ui);
  const bool handoffBlocked = !g_sleepHandoff.AllowMovies(ui->IsMenuOpen(RE::SleepWaitMenu::MENU_NAME));
  static std::uint64_t refusedMovies = 0;
  if (!handoffBlocked) {
    // Evaluate embedded SkyUI controls on the UI owner thread only. An
    // inventory hidden under its own search/quantity UI is not armed.
    if (g_inventoryReady.load() && !ReadyInventory(ui)) {
      g_inventoryReady.store(0);
      g_inventoryRequest.store(0);
    }
    g_movies(ui);
    CloseRequestedInventory(ui);
    // The opening press can arrive during the same input pass as the Show.
    // Only arm a menu after its first pass and while its lists own input.
    g_inventoryReady.store(ReadyInventory(ui));
  } else {
    g_inventoryRequest.store(0);
    g_inventoryReady.store(0);
    // Never deliver a live input callback to a picker that refused Hide.
    // The passive pass counters also report this unsupported transition.
    if (++refusedMovies <= 3 || refusedMovies % 1200 == 0)
      logger::critical("sleep/wait: blocked live handoff; picker did not close, movie/input pass refused ({})", refusedMovies);
  }
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
    const auto* main = RE::Main::GetSingleton();
    logger::info("menu UI owner: frames {} messages {} movies {} console {} mismatches {} outside {} live {} handoffBlocked {} refusedMovies {} pauses {} freezeTime {}",
      frames, totals[0], totals[1], totals[2], bad, g_outside.load(),
      g_enabled.load(), handoffBlocked, refusedMovies, ui->numPausesGame, main && main->freezeTime);
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
  // leaves ALL nine original sites intact, not half a scheduling change.
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

void GameplayMenuUnpaused::InventoryChanged(bool opened)
{
  g_inventoryLifetime.store(InventoryShortcut::NewLifetime(g_inventoryLifetime.load(), opened));
}

void GameplayMenuUnpaused::InventoryKey(const RE::ButtonEvent& button)
{
  const auto lifetime = g_inventoryLifetime.load();
  const auto ready = g_inventoryReady.load();
  const bool keyboard = button.GetDevice() == RE::INPUT_DEVICE::kKeyboard;
  if (!InventoryShortcut::Current(ready, lifetime) || !g_enabled.load(std::memory_order_acquire) ||
      !keyboard || !button.IsDown())
    return;
  const auto* controls = RE::ControlMap::GetSingleton();
  if (!controls || controls->textEntryCount != 0)
    return;
  const auto request = InventoryShortcut::Request(lifetime, ready, button.GetIDCode(),
    controls->GetMappedKey("Quick Inventory", RE::INPUT_DEVICE::kKeyboard));
  // Check the origin as well as the delivery: a top menu that handles and
  // closes on this key must not also close the inventory behind it.
  if (request && InventoryTakingInput(RE::UI::GetSingleton()))
    g_inventoryRequest.store(request);
}

void GameplayMenuUnpaused::RequestClient(bool requested)
{
  g_world.Request(requested);
}
void GameplayMenuUnpaused::WorldLoading()
{
  g_world.Loading();
  g_enabled.store(0, std::memory_order_release);
}
void GameplayMenuUnpaused::WorldLoaded(bool success)
{
  g_world.Loaded(success);
  if (!success) g_enabled.store(0, std::memory_order_release);
}
bool GameplayMenuUnpaused::HasLoadedWorld()
{
  return g_world.HasLoadedWorld();
}

void GameplayMenuUnpaused::PostLoad(void* outcome)
{
  WorldLoaded(GameplayMenuPolicy::PostLoadSucceeded(outcome));
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
  const auto statsInput = REL::ID(52518).address() + 0x46;
  g_messages = reinterpret_cast<UiPass>(REL::ID(82082).address());
  g_movies = reinterpret_cast<UiPass>(REL::ID(82083).address());
  g_console = reinterpret_cast<decltype(g_console)>(REL::ID(52950).address());
  g_commands = reinterpret_cast<decltype(g_commands)>(REL::ID(52952).address());
  g_sleepOk = reinterpret_cast<SleepCallback*>(REL::ID(52488).address());
  g_sleepCancel = reinterpret_cast<SleepCallback*>(REL::ID(52489).address());
  const auto heapCall = main + 0xadf;
  g_originalHeapCall = reinterpret_cast<decltype(g_originalHeapCall)>(heapCall + 5 +
    *reinterpret_cast<std::int32_t*>(heapCall + 1));
  std::vector<Patch> patches{
    { job + 0xb, { 0x75, 0x25 }, { 0xeb, 0x25 } },
    { main + 0xa8a, { 0xe8, 0x71, 0xd6, 0x95, 0 }, P::Bytes(5, 0x90) },
    { main + 0xaf0, { 0xe8, 0xfb, 0xe2, 0x95, 0 }, P::Bytes(5, 0x90) },
    { main + 0xaf5, { 0xe8, 0x46, 0x49, 0x33, 0, 0x48, 0x8b, 0xc8,
                     0xe8, 0xee, 0x4a, 0x33, 0 }, P::Bytes(13, 0x90) },
    { heapCall, { 0xe8, 0xec, 0xf2, 0x67, 0 }, P::Bytes(5, 0x90) },
    { show, { 0xff, 0x50, 0x20, 0x83, 0xf8, 1 }, P::Bytes(6, 0x90) },
    { map, { 0x83, 0xc8, 0x20, 0xeb, 3 }, P::Bytes(5, 0x90) },
    { stats, { 0x83, 0x4e, 0x1c, 0x20, 0x48, 0x83, 0xbe, 0xa8, 2, 0, 0, 0 }, P::Bytes(12, 0x90) },
    { statsInput, { 0x0f, 0x85, 0x64, 1, 0, 0 }, P::Bytes(6, 0x90) }
  };
  if (!Matches(patches)) return false;
  // Prepare all code and observers before changing any functional instruction.
  g_trampoline.create(1024);
  P::Bytes owner;
  P::Jump(owner, reinterpret_cast<std::uintptr_t>(&Owner));
  patches[4].after = P::Branch(0xe8, heapCall, Store(owner), 5);
  patches[5].after = P::Branch(0xe9, show,
    Store(P::Show(reinterpret_cast<std::uintptr_t>(&Show), show + 6)), 6);
  const auto enabled = reinterpret_cast<std::uintptr_t>(&g_enabled);
  patches[6].after = P::Branch(0xe9, map, Store(P::Background(enabled, map + 8, false)), 5);
  patches[7].after = P::Branch(0xe9, stats, Store(P::Background(enabled, stats + 12, true)), 12);
  patches[8].after = P::Branch(0xe9, statsInput,
    Store(P::StatsInput(enabled, statsInput + 6, statsInput + 6 + 0x164)), 6);
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
