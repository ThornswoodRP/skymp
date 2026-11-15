#pragma once
// Thornswood #6215. The menu that holds the game's cursor while the page has
// focus.
//
// The game decides on its own whether the Cursor Menu is up. After each menu
// message it handles, UI looks at the topmost open menu below depth 11 that
// is not a kAssignCursorToRenderer menu (the stack is sorted by depth), queues
// the Cursor Menu's show when that menu has kUsesCursor, and queues its hide
// otherwise (read from the game's code in a dump of 15 Nov 2026, Address
// Library id 82097). Until this file, SkyrimPlatform queued the Cursor Menu's
// show and hide itself and queued another show whenever the Cursor Menu
// closed while the page held focus. With no menu of the game's using the
// cursor, the game answered that show with a hide in the same pass, the
// hide's close event brought the next show, and the pass never ended: on 15
// Nov the chat key froze two games, each with the UI job building and tearing
// down Cursor Menus until the game was ended.
//
// So the page's focus is a menu of its own, with kUsesCursor and no movie.
// Every path the game takes a menu through tests uiMovie for null (IMenu's
// own functions, which this menu keeps, the UI pass, and the advance and
// render loops, read from the same dump), and it asks for no rendering of its
// own. While it is open the game's own rule shows the Cursor Menu and keeps
// it, including after a game menu that used the cursor closes, and when it
// closes the same rule takes the cursor down unless another menu still wants
// it. Nothing here shows or hides the Cursor Menu.
#include <string_view>

class PageFocusMenu : public RE::IMenu
{
public:
  static constexpr std::string_view MENU_NAME = "SkyrimPlatform Page Focus";

  // Over the menus kept open in the world without a cursor (BTPS and TrueHUD
  // at 0, the HUD at 2, the Fader at 3) and under the Loading Menu (9) and
  // the wait spinner (10), so a load still takes the cursor while it is up.
  static constexpr std::int8_t kDepth = 8;

  PageFocusMenu()
  {
    depthPriority = kDepth;
    menuFlags.set(Flag::kUsesCursor, Flag::kAllowSaving);
    inputContext = Context::kNone;
  }

  static RE::IMenu* Create() { return new PageFocusMenu(); }
};
