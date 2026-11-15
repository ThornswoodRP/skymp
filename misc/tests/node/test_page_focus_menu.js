'use strict';
// Thornswood #6215. The page's focus is a menu of its own with kUsesCursor
// (PageFocusMenu.h), and the game's own rule shows and hides the Cursor Menu.
//
// 15 Nov 2026, main build-1.3.69: the chat key froze Patrick's game at
// 3:27:20 PM and a playtester's at 3:39:33 PM, each right after "focus
// claimed by chat". In Patrick's full dump the game's UI job had been
// draining a queue of nothing but Cursor Menu shows and hides for two
// minutes (the 64-entry pool full, the 100-slot ring wrapped), building and
// tearing down a Cursor Menu each time, while the main loop waited on it.
// After each menu message the game queues the Cursor Menu's hide when the
// topmost open menu below depth 11 has no kUsesCursor (id 82097), and
// SkyrimPlatform queued its show again from the Cursor Menu's own close event
// while the page held focus, so the pass never ended.
//
// The first half models the game's UI pass and its cursor rule, with the old
// and the new SkyrimPlatform in it. The second half reads the sources, since
// their functions need the game.
const fs = require('node:fs'), path = require('node:path');
const dir = path.join(__dirname, '../../../skyrim-platform/src/platform_se/skyrim_platform');
const read = (f) => fs.readFileSync(path.join(dir, f), 'utf8');
let failed = 0;
function check(name, ok) {
  console.log((ok ? 'PASS ' : 'FAIL ') + name);
  if (!ok) failed++;
}

const CURSOR = 'Cursor Menu', FOCUS = 'SkyrimPlatform Page Focus';
const USES_CURSOR = 0x4, ASSIGN_CURSOR = 0x10000;

// The game's UI as read from the dump: one queue, drained until empty, and
// the cursor rule run after each show or hide it handles. The stack is kept
// sorted by depth, newest last among equals, and the rule takes the first
// menu from the top below depth 11 that does not assign the cursor to the
// renderer. Depths and flags are the dump's.
function makeGame(oldPlatform) {
  const g = {
    menus: {
      'HUD Menu': { depth: 2, flags: 0x18942 },
      'BTPS Menu': { depth: 0, flags: 0x942 },
      'TrueHUD': { depth: 0, flags: 0x942 },
      [CURSOR]: { depth: 13, flags: 0x8800 },
      'Loading Menu': { depth: 9, flags: 0x20803 },
      'InventoryMenu': { depth: 0, flags: USES_CURSOR },
      [FOCUS]: { depth: 8, flags: USES_CURSOR | 0x800 },
    },
    stack: ['TrueHUD', 'BTPS Menu', 'HUD Menu'],
    queue: [],
    focus: false,
    handled: 0,
    built: 0,
  };
  g.open = (name) => g.stack.includes(name);
  g.pending = (name, type) => g.queue.some((m) => m.name === name && m.type === type);
  g.cursorRule = () => {
    const below = g.stack.filter((n) => g.menus[n].depth < 11 && !(g.menus[n].flags & ASSIGN_CURSOR))
      .map((n, i) => [n, i]).sort((a, b) => g.menus[b[0]].depth - g.menus[a[0]].depth || b[1] - a[1])
      .map((p) => p[0]);
    const top = below[0];
    if (top && g.menus[top].flags & USES_CURSOR) {
      if (!g.open(CURSOR) && !g.pending(CURSOR, 'show')) g.queue.push({ name: CURSOR, type: 'show' });
    } else if (g.open(CURSOR) || g.pending(CURSOR, 'show')) {
      g.queue.push({ name: CURSOR, type: 'hide' });
    }
  };
  // The old SkyrimPlatform's sink: the Cursor Menu closing while the page
  // holds focus queued its show again (CursorFocus.h, #2384).
  g.closed = (name) => {
    if (oldPlatform && name === CURSOR && g.focus) g.queue.push({ name: CURSOR, type: 'show' });
  };
  // One pass. Returns false when it had not emptied the queue after 10000
  // messages, which in the game is a frame that never ends.
  g.pass = () => {
    let n = 0;
    while (g.queue.length) {
      if (++n > 10000) return false;
      const m = g.queue.shift();
      g.handled++;
      if (m.type === 'show' && !g.open(m.name)) {
        g.stack.push(m.name);
        if (m.name === CURSOR) g.built++;
      } else if (m.type === 'hide' && g.open(m.name)) {
        g.stack = g.stack.filter((s) => s !== m.name);
        g.closed(m.name);
      } else {
        continue;
      }
      g.cursorRule();
    }
    return true;
  };
  // setFocused as each SkyrimPlatform did it. The old one queued the Cursor
  // Menu's show and hide itself; the new one opens and closes its own menu.
  g.setFocused = (f) => {
    if (g.focus === f) return;
    g.focus = f;
    g.queue.push({ name: oldPlatform ? CURSOR : FOCUS, type: f ? 'show' : 'hide' });
  };
  return g;
}

{
  // The chat key on 15 Nov: two owners of focus raise, lower and raise it in
  // one frame (skymp5-client's Enter binding and thornswood-front's claim).
  const old = makeGame(true);
  old.setFocused(true); old.setFocused(false); old.setFocused(true);
  check('the old platform never ends the pass for a raise, lower and raise', !old.pass());
  check('and builds Cursor Menus the whole time', old.built > 1000);

  const g = makeGame(false);
  g.setFocused(true); g.setFocused(false); g.setFocused(true);
  check('the focus menu ends the same pass', g.pass());
  check('with the focus menu and the cursor open', g.open(FOCUS) && g.open(CURSOR));
  check('having built the Cursor Menu at most twice', g.built <= 2);
}
{
  const g = makeGame(false);
  g.setFocused(true);
  g.pass();
  check('a plain raise opens the cursor', g.open(CURSOR));
  // A game menu that uses the cursor opens over the page and closes again.
  g.queue.push({ name: 'InventoryMenu', type: 'show' });
  g.pass();
  g.queue.push({ name: 'InventoryMenu', type: 'hide' });
  check('a game menu closing over the page ends its pass', g.pass());
  check('and the cursor stays, kept by the game for the focus menu', g.open(CURSOR) && g.built === 1);
  g.setFocused(false);
  g.pass();
  check('letting go of focus takes the cursor down', !g.open(FOCUS) && !g.open(CURSOR));
  // Under a game menu that uses the cursor, that menu keeps it.
  g.queue.push({ name: 'InventoryMenu', type: 'show' });
  g.pass();
  g.setFocused(true); g.pass(); g.setFocused(false); g.pass();
  check('letting go of focus under a game menu leaves that menu its cursor',
        g.open('InventoryMenu') && g.open(CURSOR));
  // A load screen over the page takes the cursor while it is up.
  g.queue.push({ name: 'InventoryMenu', type: 'hide' });
  g.setFocused(true);
  g.queue.push({ name: 'Loading Menu', type: 'show' });
  check('a load over the page ends its pass', g.pass());
  check('with no cursor while it loads', !g.open(CURSOR));
  g.queue.push({ name: 'Loading Menu', type: 'hide' });
  g.pass();
  check('and the cursor back for the page once it has loaded', g.open(CURSOR) && g.open(FOCUS));
}
{
  // Many raises and lowers, as fast as plugins can make them.
  const g = makeGame(false);
  let ended = true;
  for (let i = 0; i < 200; i++) { g.setFocused(i % 2 === 0); if (i % 3 === 0) ended = g.pass() && ended; }
  ended = g.pass() && ended;
  check('any run of raises and lowers ends every pass', ended);
  check('and ends with the cursor matching the focus', g.open(CURSOR) === g.focus && g.open(FOCUS) === g.focus);
}

const body = (src, signature) => {
  const at = src.indexOf(signature);
  if (at < 0) return null;
  const open = src.indexOf('{', at);
  let depth = 0;
  for (let i = open; i < src.length; i++) {
    if (src[i] === '{') depth++;
    else if (src[i] === '}' && --depth === 0) return src.slice(open, i + 1);
  }
  return null;
};
const code = (s) => s.replace(/\/\/[^\n]*/g, '').replace(/\/\*[\s\S]*?\*\//g, '');

const menu = code(read('PageFocusMenu.h'));
check('the focus menu is named as the model names it', menu.includes('"' + FOCUS + '"'));
check('it uses the cursor', /menuFlags\.set\([^)]*kUsesCursor/.test(menu));
const depth = /kDepth\s*=\s*(\d+)/.exec(menu);
check('it sits over the world menus plugins keep open (0 to 3) and under the Loading Menu (9)',
      !!depth && Number(depth[1]) > 3 && Number(depth[1]) < 9);
check('it changes no input context', /inputContext\s*=\s*Context::kNone/.test(menu));
check('it carries no movie', !/uiMovie/.test(menu));
check('it does not pause the game', !/kPausesGame|kModal|kUsesMenuContext/.test(menu));
check('it asks for no rendering of its own, which could read a movie',
      !/kCustomRendering|kRendersOffscreenTargets|kTopmostRenderedMenu|kAssignCursorToRenderer/.test(menu));

const src = code(read('BrowserApiTilted.cpp'));
const set = body(src, 'void SetFocusedImpl(bool newFocus)');
check('setFocused opens or closes the focus menu', !!set && /QueueFocusMenu\(newFocus\)/.test(set));
check('nothing in the browser queues the Cursor Menu', !/AddMessage\(\s*RE::CursorMenu::MENU_NAME/.test(src));
const sink = body(src, 'class MenuSink');
check('no menu event is answered with a message', !!sink && !/AddMessage|QueueFocusMenu|SetFocusedImpl/.test(sink));
const watch = body(src, 'void BrowserApiTilted::WatchMenus()');
check('WatchMenus registers the focus menu', !!watch && /Register\(PageFocusMenu::MENU_NAME,\s*PageFocusMenu::Create\)/.test(watch));
check('and opens it for a focus taken before it was registered',
      !!watch && /if \(focus\)\s*\{\s*QueueFocusMenu\(true\);/.test(watch));
check('CursorFocus is gone', !fs.existsSync(path.join(dir, 'CursorFocus.h')));
for (const f of fs.readdirSync(dir).filter((f) => /\.(cpp|h)$/.test(f))) {
  const s = code(read(f));
  if (/AddMessage\(\s*RE::CursorMenu::MENU_NAME|AddMessage\(\s*"Cursor Menu"/.test(s)) {
    check(f + ' queues no Cursor Menu message', false);
  }
}

if (failed) { console.log(failed + ' check(s) failed'); process.exitCode = 1; }
