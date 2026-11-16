#include <NirnLabUIPlatformAPI/API.h>

#include "BrowserApiTilted.h"
#include "NullPointerException.h"
#include "PageFocusMenu.h"
#include "TPOverlayService.h"
#include <ui/LoadingScreen.h>

namespace {

// Thornswood #6215: the page's focus is PageFocusMenu, open while the page
// holds focus, and the game's own cursor rule follows it (PageFocusMenu.h).
// Queued, not done: the game opens or closes it on its next UI pass, in the
// order asked, so a raise and a lower in one frame end where the last one
// left them.
void QueueFocusMenu(bool open)
{
  auto msgQ = RE::UIMessageQueue::GetSingleton();
  if (!msgQ) {
    spdlog::error("browser: no UI message queue for the page's focus menu");
    return;
  }
  msgQ->AddMessage(
    PageFocusMenu::MENU_NAME,
    open ? RE::UI_MESSAGE_TYPE::kShow : RE::UI_MESSAGE_TYPE::kHide, nullptr);
}

// Whether the focus menu is registered with the game's UI yet (WatchMenus,
// at kDataLoaded). Focus asked for before that is queued once it is.
std::atomic<bool> g_focusMenuRegistered = false;

// Thornswood #6200: where the cursor comes up when the page takes focus. On
// 16 Nov 2026 the page's first mouse move was at 3429,0 of a 3440x1440
// window, 0.7 s after the focus was taken, and on 15 Nov at 3399,0: the top
// edge, a few pixels from the right corner. The cursor is drawn at the game's
// own menu cursor (MenuScreenData::mousePos, main.cpp), so this says where
// that is, whether the Cursor Menu is up, and where the Windows pointer is,
// at the moments that decide it. Measurement only: nothing here moves either.
void LogCursor(const char* when)
{
  auto ui = RE::UI::GetSingleton();
  const bool cursorMenu = ui && ui->IsMenuOpen(RE::CursorMenu::MENU_NAME);
  auto data = RE::MenuScreenData::GetSingleton();
  const float x = data ? data->mousePos.x : -1.f;
  const float y = data ? data->mousePos.y : -1.f;
  POINT p{};
  HWND front = GetForegroundWindow();
  const bool pointer = GetCursorPos(&p) && front && ScreenToClient(front, &p);
  RECT r{};
  const bool size = front && GetClientRect(front, &r);
  spdlog::info("cursor, {}: the game's menu cursor at {:.0f},{:.0f}, Cursor "
               "Menu {}, the Windows pointer at {},{} of the window in "
               "front{} ({}x{})",
               when, x, y, cursorMenu ? "open" : "closed", pointer ? p.x : -1,
               pointer ? p.y : -1, pointer ? "" : " (unknown)",
               size ? r.right - r.left : -1, size ? r.bottom - r.top : -1);
}

inline CEFUtils::MyChromiumApp& GetApp()
{
  auto overlayService = OverlayService::GetInstance();
  if (!overlayService) {
    throw NullPointerException("overlayService");
  }

  auto app = overlayService->GetMyChromiumApp();
  if (!app) {
    throw NullPointerException("app");
  }

  return *app;
}
}

Napi::Value BrowserApiTilted::SetVisible(const Napi::CallbackInfo& info)
{
  bool& v = CEFUtils::DX11RenderHandler::Visible();
  v = NapiHelper::ExtractBoolean(info[0], "visible");
  return info.Env().Undefined();
}

Napi::Value BrowserApiTilted::IsVisibleJS(const Napi::CallbackInfo& info)
{
  return Napi::Boolean::New(info.Env(), IsVisible());
}

bool BrowserApiTilted::IsVisible()
{
  return CEFUtils::DX11RenderHandler::Visible();
}

namespace {
void SetFocusedImpl(bool newFocus)
{
  bool& v = CEFUtils::DInputHook::ChromeFocus();
  if (v == newFocus) {
    return;
  }
  v = newFocus;
  spdlog::info("browser: focus {}{}", newFocus ? "taken" : "let go",
               g_focusMenuRegistered ? ""
                                     : ", the focus menu follows once the "
                                       "game's data has loaded");
  if (g_focusMenuRegistered) {
    QueueFocusMenu(newFocus);
  }
  if (newFocus) {
    LogCursor("focus taken");
  }
}
}

Napi::Value BrowserApiTilted::SetFocused(const Napi::CallbackInfo& info)
{
  SetFocusedImpl(NapiHelper::ExtractBoolean(info[0], "focused"));
  return info.Env().Undefined();
}

void BrowserApiTilted::ShowAtStart()
{
  // Thornswood: shown, not focused. Focus hands the page the game's mouse,
  // keyboard and Cursor Menu, and nothing here can follow the Cursor Menu
  // before the game's data has loaded (WatchMenus below). Focused from here,
  // the page held the input on 31 Oct 2026 with the Cursor Menu closed: no
  // cursor drawn, no mouse move reaching the page, the game itself deaf,
  // and a click still landing at the middle of the screen, where it picked
  // the character drawn there. Nothing on the page can act before a plugin
  // runs anyway; the plugin that shows the choice focuses it then.
  CEFUtils::DX11RenderHandler::Visible() = true;
}

namespace {
// Thornswood: the Loading Menu reaches the page from the moment the game's
// data has loaded, whether or not a plugin listens for menuOpen or
// menuClose. EventHandler's own MenuOpenCloseEvent sink is only added once a
// plugin subscribes to one of them. The Cursor Menu is the game's to show and
// hide (PageFocusMenu.h, Thornswood #6215); nothing here answers its events,
// they are only logged with where the cursor is (Thornswood #6200).
class MenuSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
  RE::BSEventNotifyControl ProcessEvent(
    const RE::MenuOpenCloseEvent* event,
    RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
  {
    if (!event) {
      return RE::BSEventNotifyControl::kContinue;
    }
    if (event->menuName == RE::LoadingMenu::MENU_NAME) {
      BrowserApiTilted::OnLoadingMenu(event->opening);
    } else if (event->menuName == RE::CursorMenu::MENU_NAME) {
      LogCursor(event->opening ? "Cursor Menu opening"
                               : "Cursor Menu closing");
    }
    return RE::BSEventNotifyControl::kContinue;
  }
};

MenuSink g_menuSink;
bool g_watchingMenus = false;
}

void BrowserApiTilted::WatchMenus()
{
  if (g_watchingMenus) {
    return;
  }
  auto ui = RE::UI::GetSingleton();
  if (!ui) {
    spdlog::error("browser: no UI to watch the Loading Menu on or to give "
                  "the page's focus menu to");
    return;
  }
  // The same source EventHandler's sink is added to (EventUtils.h).
  RE::BSTEventSource<RE::MenuOpenCloseEvent>* source = ui;
  source->AddEventSink(&g_menuSink);
  g_watchingMenus = true;

  ui->Register(PageFocusMenu::MENU_NAME, PageFocusMenu::Create);
  g_focusMenuRegistered = true;
  const bool focus = CEFUtils::DInputHook::ChromeFocus();
  spdlog::info("browser: watching the Loading Menu; the page's focus menu "
               "is registered{}",
               focus ? " and opened for the focus already taken" : "");
  if (focus) {
    QueueFocusMenu(true);
  }
  LogCursor("game data loaded");
}

void BrowserApiTilted::OnLoadingMenu(bool opening)
{
  // Thornswood #3130: called from WatchMenus' sink, on the game's thread, as
  // the Loading Menu itself opens or closes, which is before and after the
  // load. The plugins hear of it only after the load (LoadingScreen.h). The
  // page is drawn while it is open, and told.
  CEFUtils::DX11RenderHandler::Loading() = opening;
  spdlog::info(opening ? "Loading Menu opened, the page is drawn over it"
                       : "Loading Menu closed");
  auto overlayService = OverlayService::GetInstance();
  if (!overlayService) {
    return;
  }
  auto app = overlayService->GetMyChromiumApp();
  if (!app) {
    return;
  }
  app->ExecuteJavaScript(LoadingScreen::Script(opening));
}

Napi::Value BrowserApiTilted::IsFocused(const Napi::CallbackInfo& info)
{
  return Napi::Boolean::New(info.Env(), CEFUtils::DInputHook::ChromeFocus());
}

Napi::Value BrowserApiTilted::LoadUrl(const Napi::CallbackInfo& info)
{
  auto str = NapiHelper::ExtractString(info[0], "url");
  return Napi::Boolean::New(info.Env(), GetApp().LoadUrl(str.data()));
}

Napi::Value BrowserApiTilted::ExecuteJavaScript(const Napi::CallbackInfo& info)
{
  auto str = NapiHelper::ExtractString(info[0], "src");
  GetApp().ExecuteJavaScript(str);
  return info.Env().Undefined();
}
