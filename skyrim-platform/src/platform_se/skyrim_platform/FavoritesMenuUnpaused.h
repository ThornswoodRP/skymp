#pragma once
// Thornswood #6408. The quick favourites menu (Q) keeps the world running.
//
// FavoritesMenu is one of the game's own pausing menus: its constructor sets
// kPausesGame | kUpdateUsesCursor | kInventoryItemMenu | kCustomRendering
// (CommonLibSSE-NG FavoritesMenu.h). In a world other people share nothing
// should stop for one person's menu, and from September (#87) Skyrim Souls RE
// kept this one unpaused. Collection revision 2 took Skyrim Souls RE out on
// 3 Oct, and the Q menu has paused the world since; on 16 Nov 2026 Patrick's
// skse64.log loaded 31 plugins without it, and the newest log that shows it
// loading is from 26 Sep.
//
// The game builds a menu from the creator registered for its name in
// UI::menuMap each time the menu is shown, and counts the pause from the
// built menu's flags when it is shown and again when it is hidden. So the
// creator registered for FavoritesMenu is wrapped once, at kDataLoaded, by one
// that builds the game's menu and takes kPausesGame off it before the game
// sees it: the show and the hide then both find it without the flag, and the
// menu is otherwise the game's own.
namespace FavoritesMenuUnpaused {
void Install();
}
