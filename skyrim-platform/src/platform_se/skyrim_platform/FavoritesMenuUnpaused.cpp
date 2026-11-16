#include "FavoritesMenuUnpaused.h"

namespace {
// The game's own creator for FavoritesMenu, kept to build the menu with.
RE::UI::Create_t* g_gameCreate = nullptr;

RE::IMenu* CreateUnpaused()
{
  RE::IMenu* menu = g_gameCreate();
  if (menu) {
    menu->menuFlags.reset(RE::IMenu::Flag::kPausesGame);
  }
  return menu;
}
}

void FavoritesMenuUnpaused::Install()
{
  if (g_gameCreate) {
    return;
  }
  auto ui = RE::UI::GetSingleton();
  if (!ui) {
    logger::error("favourites menu: no UI, so it still pauses the world");
    return;
  }
  auto it = ui->menuMap.find(RE::BSFixedString(RE::FavoritesMenu::MENU_NAME));
  if (it == ui->menuMap.end() || !it->second.create) {
    logger::error("favourites menu: the game has no creator registered for "
                  "it, so it still pauses the world");
    return;
  }
  g_gameCreate = it->second.create;
  it->second.create = CreateUnpaused;
  logger::info("favourites menu: built without its pause from now on");
}
