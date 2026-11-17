'use strict';
// Thornswood #6428. The Escape journal keeps the world running.
//
// The game's journal pause flag is removed before show and hide count it.
// This holds the registration boundary and repeated open/close contract;
// the native companion test executes the actual C++ wrapper.
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
  const g = { numPausesGame: 0, menuMap: { JournalMenu: { create: () => ({ flags: FAVORITES_FLAGS }) } }, open: {} };
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
  g.show('JournalMenu');
  check('before: the game pauses while the Escape menu is open', g.numPausesGame === 1);
  g.hide('JournalMenu');
  check('before: and stops pausing when it closes', g.numPausesGame === 0);
}

// After: the creator wrapped once at kDataLoaded.
{
  const g = makeGame();
  const gameCreate = g.menuMap.JournalMenu.create;
  g.menuMap.JournalMenu.create = () => {
    const m = gameCreate();
    if (m) m.flags &= ~PAUSES;
    return m;
  };
  for (let i = 0; i < 3; i++) {
    g.show('JournalMenu');
    check('after: open ' + (i + 1) + ', nothing pauses', g.numPausesGame === 0);
    check('after: open ' + (i + 1) + ', the menu keeps its other flags',
      g.open.JournalMenu.flags === (FAVORITES_FLAGS & ~PAUSES));
    g.hide('JournalMenu');
    check('after: close ' + (i + 1) + ', the count is not taken below zero', g.numPausesGame === 0);
  }
}

// The sources.
const cpp = read('JournalMenuUnpaused.cpp');
const events = read('EventHandler.cpp');
check('the wrapper builds with the game\'s own creator', /g_gameCreate\(\)/.test(cpp));
check('and takes only kPausesGame off what it built',
  /menuFlags\.reset\(RE::IMenu::Flag::kPausesGame\)/.test(cpp) &&
  !/menuFlags\.(set|reset)\((?![^)]*kPausesGame\))/.test(cpp));
check('it replaces the creator registered for JournalMenu',
  /menuMap\.find\(RE::BSFixedString\(RE::JournalMenu::MENU_NAME\)\)/.test(cpp) &&
  /it->second\.create = CreateUnpaused/.test(cpp));
check('it wraps only once, so the wrapper never calls itself',
  /if \(g_gameCreate\) \{\s*return;/.test(cpp));
check('it says when it could not, instead of failing quietly',
  (cpp.match(/logger::error/g) || []).length >= 2);
const loaded = events.slice(events.indexOf('case SKSE::MessagingInterface::kDataLoaded'));
check('it is installed at kDataLoaded, before the plugins run',
  loaded.indexOf('JournalMenuUnpaused::Install()') !== -1 &&
  loaded.indexOf('JournalMenuUnpaused::Install()') < loaded.indexOf('EventManager::Init()'));

if (failed) {
  console.log(failed + ' check(s) failed');
  process.exit(1);
}
console.log('all checks passed');
