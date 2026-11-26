'use strict';
// Thornswood: the browser is shown at start but not focused before the game's
// data has loaded; from that moment the Loading Menu reaches the page, the
// page's focus menu is registered (#6215 replaced CursorFocus with it), and a
// page shown at start takes the focus (#6200).
//
// 31 Oct 2026, dev 20261031-143003-dev: [Browser] ShowAtStart focused the page
// at 14:59:44, fifteen seconds before the game's data loaded and twenty-five
// before any plugin ran. The Cursor Menu's events reached CursorFocus only
// through EventHandler's MenuOpenCloseEvent sink, which is added once a plugin
// subscribes to menuOpen or menuClose. So the page held the game's input with
// the Cursor Menu closed: no cursor drawn, no mouse move reaching the page,
// and a click landing at the middle of the screen (1720,720 of 3440x1440, the
// game's untouched cursor position), where it picked the character drawn
// there. These checks read the sources, since the functions need the game.
const fs = require('node:fs'), path = require('node:path');
const dir = path.join(__dirname, '../../../skyrim-platform/src/platform_se/skyrim_platform');
const read = (f) => fs.readFileSync(path.join(dir, f), 'utf8');
let failed = 0;
function check(name, ok) {
  console.log((ok ? 'PASS ' : 'FAIL ') + name);
  if (!ok) failed++;
}
// The body of a function, from its opening brace to the matching close.
function body(src, signature) {
  const at = src.indexOf(signature);
  if (at < 0) return null;
  const open = src.indexOf('{', at);
  let depth = 0;
  for (let i = open; i < src.length; i++) {
    if (src[i] === '{') depth++;
    else if (src[i] === '}' && --depth === 0) return src.slice(open, i + 1);
  }
  return null;
}
const code = (s) => s.replace(/\/\/[^\n]*/g, '').replace(/\/\*[\s\S]*?\*\//g, '');

const tilted = read('BrowserApiTilted.cpp');
const show = body(tilted, 'void BrowserApiTilted::ShowAtStart()');
check('Tilted ShowAtStart exists', !!show);
check('Tilted ShowAtStart shows the page', !!show && /Visible\(\)\s*=\s*true/.test(code(show)));
check('Tilted ShowAtStart does not focus it', !!show && !/SetFocusedImpl|ChromeFocus|Queue\(/.test(code(show)));

const nirn = read('BrowserApiNirnLab.cpp');
const ctor = body(nirn, 'BrowserApiNirnLab::BrowserApiNirnLab()');
check('NirnLab start shows the page', !!ctor && /wantedIsVisible\s*=\s*true/.test(code(ctor)));
check('NirnLab start does not focus it', !!ctor && !/wantedIsFocused\s*=\s*true/.test(code(ctor)));

const watch = body(tilted, 'void BrowserApiTilted::WatchMenus()');
check('WatchMenus adds a sink of its own to the UI', !!watch && /AddEventSink\(&g_menuSink\)/.test(code(watch)));

// #6200, 16 to 25 Nov 2026: the game's Cursor Menu came up with the main menu
// under the page shown at start, and the plugin focused the page 1.4 to 22
// seconds later; in 17 of 54 starts the game's cursor had moved where nobody
// could see it, in 10 to an edge. So the page shown at start takes the focus
// in WatchMenus, after the focus menu is registered so the game keeps its
// cursor up for it, and only when nothing has focused or hidden it already.
check('ShowAtStart notes that it showed the page', !!show && /g_shownAtStart\s*=\s*true/.test(code(show)));
const w6200 = watch ? code(watch) : '';
const reg = w6200.indexOf('g_focusMenuRegistered = true');
const take = w6200.search(/if \(g_shownAtStart && IsVisible\(\) && !focus\)\s*\{[^}]*SetFocusedImpl\(true\);[^}]*\}/);
check('WatchMenus focuses a page shown at start that is still shown and not focused', take >= 0);
check('  after the focus menu is registered, so the game keeps its cursor up for it', reg >= 0 && take > reg);
check('  and through SetFocusedImpl, which says so in the log and queues the focus menu',
      /QueueFocusMenu\(newFocus\)/.test(code(body(tilted, 'void SetFocusedImpl(bool newFocus)') || '')));
check('it is the only focus WatchMenus or ShowAtStart takes', (w6200.match(/SetFocusedImpl\(/g) || []).length === 1);
const sink = body(tilted, 'class MenuSink');
// It may log where the cursor is when the Cursor Menu opens or closes
// (#6200), and nothing more: no show, hide or focus from its events.
const cursorBranch = sink && (code(sink).match(/CursorMenu::MENU_NAME\)\s*\{([^}]*)\}/) || [])[1];
check('its sink answers nothing of the Cursor Menu, which is the game\'s (#6215)',
      !!sink && (!/CursorMenu/.test(code(sink)) ||
        (!!cursorBranch && /^\s*LogCursor\([^;]*\);\s*$/.test(cursorBranch))) &&
      !/AddMessage|QueueFocusMenu|SetFocused|ChromeFocus/.test(code(sink)));
check('its sink hands the Loading Menu to OnLoadingMenu',
      !!sink && /LoadingMenu::MENU_NAME\)\s*\{\s*BrowserApiTilted::OnLoadingMenu\(event->opening\)/.test(code(sink)));

const handler = code(read('EventHandler.cpp'));
const loaded = handler.slice(handler.indexOf('case SKSE::MessagingInterface::kDataLoaded'));
const w = loaded.indexOf('BrowserApiTilted::WatchMenus()');
const t = loaded.indexOf('TickHandler::GetSingleton()->Update()');
check('kDataLoaded starts WatchMenus before the first tick runs a plugin', w >= 0 && t >= 0 && w < t);
const menuEvent = body(handler, 'EventHandler::ProcessEvent(\n  const RE::MenuOpenCloseEvent*');
check("the plugins' lazy menu sink no longer calls the browser itself (no double calls)",
      !!menuEvent && !/OnCursorMenu|OnLoadingMenu|QueueFocusMenu/.test(menuEvent));

if (failed) { console.log(failed + ' check(s) failed'); process.exitCode = 1; }
