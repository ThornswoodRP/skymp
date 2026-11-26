#pragma once
#include "Settings.h"

#include "NapiHelper.h"

namespace BrowserApiTilted {
Napi::Value SetVisible(const Napi::CallbackInfo& info);
Napi::Value IsVisibleJS(const Napi::CallbackInfo& info);
bool IsVisible();
Napi::Value SetFocused(const Napi::CallbackInfo& info);
// Thornswood #3129: shown the way setVisible(true) does it, before any plugin
// can call it. Not focused yet: WatchMenus focuses it.
void ShowAtStart();
// From kDataLoaded: the Loading Menu reaches OnLoadingMenu whether or not a
// plugin listens for menus, the page's focus menu (PageFocusMenu.h) is
// registered with the game's UI, and a page shown at start takes the focus
// as the game's cursor comes up (Thornswood #6200).
void WatchMenus();
Napi::Value IsFocused(const Napi::CallbackInfo& info);
Napi::Value LoadUrl(const Napi::CallbackInfo& info);
Napi::Value ExecuteJavaScript(const Napi::CallbackInfo& info);
// Thornswood #3130: the game's Loading Menu opened or closed (WatchMenus).
void OnLoadingMenu(bool opening);
}
