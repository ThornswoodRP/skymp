'use strict';
// Thornswood #6408. The quick favourites menu (Q) keeps the world running.
//
// FavoritesMenu's constructor sets kPausesGame (CommonLibSSE-NG
// FavoritesMenu.h). Skyrim Souls RE kept it unpaused from September (#87)
// until collection revision 2 took that mod out on 3 Oct; on 16 Nov 2026
// Patrick's game loaded 31 SKSE plugins without it and the Q menu paused the
// world. SkyrimPlatform now wraps the game's creator for the menu and takes
// the flag off what it builds.
//
// The first half models how the game counts a pause: from the built menu's
// flags when it is shown and again when it is hidden. The second half reads
// the sources, since their functions need the game.
const fs = require('node:fs'), path = require('node:path');
const dir = path.join(__dirname, '../../../skyrim-platform/src/platform_se/skyrim_platform');
const read = (f) => fs.readFileSync(path.join(dir, f), 'utf8');
let failed = 0;
function check(name, ok) {
  console.log((ok ? 'PASS ' : 'FAIL ') + name);
  if (!ok) failed++;
}

const PAUSES = 0x1, UPDATE_USES_CURSOR = 0x400, INVENTORY_ITEM = 0x2000, CUSTOM_RENDERING = 0x8000;
const FAVORITES_FLAGS = PAUSES | UPDATE_USES_CURSOR | INVENTORY_ITEM | CUSTOM_RENDERING;

function makeGame() {
  const g = { numPausesGame: 0, menuMap: { FavoritesMenu: { create: () => ({ flags: FAVORITES_FLAGS }) } }, open: {} };
  g.show = (name) => {
    const m = g.menuMap[name].create();
    g.open[name] = m;
    if (m.flags & PAUSES) g.numPausesGame++;
  };
  g.hide = (name) => {
    const m = g.open[name];
    if (m.flags & PAUSES) g.numPausesGame--;
    delete g.open[name];
  };
  return g;
}

// Before: the game's own creator.
{
  const g = makeGame();
  g.show('FavoritesMenu');
  check('before: the game pauses while the Q menu is open', g.numPausesGame === 1);
  g.hide('FavoritesMenu');
  check('before: and stops pausing when it closes', g.numPausesGame === 0);
}

// After: the creator wrapped once at kDataLoaded.
{
  const g = makeGame();
  const gameCreate = g.menuMap.FavoritesMenu.create;
  g.menuMap.FavoritesMenu.create = () => {
    const m = gameCreate();
    if (m) m.flags &= ~PAUSES;
    return m;
  };
  for (let i = 0; i < 3; i++) {
    g.show('FavoritesMenu');
    check('after: open ' + (i + 1) + ', nothing pauses', g.numPausesGame === 0);
    check('after: open ' + (i + 1) + ', the menu keeps its other flags',
      g.open.FavoritesMenu.flags === (FAVORITES_FLAGS & ~PAUSES));
    g.hide('FavoritesMenu');
    check('after: close ' + (i + 1) + ', the count is not taken below zero', g.numPausesGame === 0);
  }
}

// The sources.
const cpp = read('FavoritesMenuUnpaused.cpp');
const events = read('EventHandler.cpp');
check('the wrapper builds with the game\'s own creator', /g_gameCreate\(\)/.test(cpp));
check('and takes only kPausesGame off what it built',
  /menuFlags\.reset\(RE::IMenu::Flag::kPausesGame\)/.test(cpp) &&
  !/menuFlags\.(set|reset)\((?![^)]*kPausesGame\))/.test(cpp));
check('it replaces the creator registered for FavoritesMenu',
  /menuMap\.find\(RE::BSFixedString\(RE::FavoritesMenu::MENU_NAME\)\)/.test(cpp) &&
  /it->second\.create = CreateUnpaused/.test(cpp));
check('it wraps only once, so the wrapper never calls itself',
  /if \(g_gameCreate\) \{\s*return;/.test(cpp));
check('it says when it could not, instead of failing quietly',
  (cpp.match(/logger::error/g) || []).length >= 2);
const loaded = events.slice(events.indexOf('case SKSE::MessagingInterface::kDataLoaded'));
check('it is installed at kDataLoaded, before the plugins run',
  loaded.indexOf('FavoritesMenuUnpaused::Install()') !== -1 &&
  loaded.indexOf('FavoritesMenuUnpaused::Install()') < loaded.indexOf('EventManager::Init()'));

if (failed) {
  console.log(failed + ' check(s) failed');
  process.exit(1);
}
console.log('all checks passed');
