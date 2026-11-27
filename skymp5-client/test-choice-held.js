// test-choice-held.js
//
//     node skymp5-client/test-choice-held.js
//
// Thornswood #4616. While the character choice is up at the main menu,
// browserService.ts hid the page for the main menu (a "bad" menu it only
// shows the page again after, and the main menu stays open until the pick
// loads), hid it again when the HUD closed on the way back to the menu, and
// took its mouse away on Escape and F6. thornswood-front.js put it back four
// times a second. Now thornswood-front.js says on globalThis when it holds
// the choice (__thornswoodChoiceHeld), and while it does the client:
//   1. does not hide the page for the main menu, and keeps no record of it;
//   2. does not hide it when the HUD closes;
//   3. leaves its focus and its visibility alone on Escape, F6 and F2;
//   4. hides it for the console, as before, and shows it with the mouse on it
//      again when the console closes.
// And once the choice is not held, every one of them is as before.
//
// WHAT THIS RUNS. The real browserService.ts, its TypeScript types taken off,
// over a stand-in sp.browser and controller that record every call.
// BROWSER=<file> runs it against another copy; against the code before the
// change cases 1 to 4 fail.
'use strict';

const fs = require('fs');
const path = require('path');

const ROOT = __dirname;
const BROWSER = process.env.BROWSER ||
  path.join(ROOT, 'src', 'services', 'services', 'browserService.ts');

let pass = 0;
let fail = 0;
function yes(what, cond) {
  if (cond) { pass++; console.log('PASS  ' + what); }
  else { fail++; console.log('FAIL  ' + what); }
}

function stripTypes(src) {
  const mod = require('module');
  if (typeof mod.stripTypeScriptTypes === 'function') {
    const warn = process.emitWarning;
    process.emitWarning = function () { };
    try { return mod.stripTypeScriptTypes(src, { mode: 'transform' }); }
    finally { process.emitWarning = warn; }
  }
  let ts = null;
  try { ts = require('typescript'); } catch (e) { ts = null; }
  if (ts && typeof ts.transpileModule === 'function') {
    return ts.transpileModule(src, {
      compilerOptions: { target: ts.ScriptTarget.ES2019, module: ts.ModuleKind.ESNext },
    }).outputText;
  }
  throw new Error('needs Node 22.13 or newer, or the typescript package (yarn install here)');
}

// Same loader as test-connection-denied.js: every import comes from stubs.
function load(file, stubs) {
  let js = stripTypes(fs.readFileSync(file, 'utf8').split('\r').join(''));
  js = js.replace(/^import\s+\{([^}]*)\}\s+from\s+["']([^"']+)["'];?/gm, function (m, names, from) {
    const list = names.split(',').map(function (n) { return n.trim(); }).filter(Boolean)
      .map(function (n) { return n.replace(/^type\s+/, '').replace(/\s+as\s+/, ': '); });
    return 'const { ' + list.join(', ') + ' } = __stub(' + JSON.stringify(from) + ');';
  });
  const exported = [];
  js = js.replace(/^export\s+(class|const|let|function)\s+(\w+)/gm, function (m, kind, name) {
    exported.push(name);
    return kind + ' ' + name;
  });
  js += '\nreturn { ' + exported.join(', ') + ' };';
  return new Function('__stub', js)(function (from) { return stubs[from] || {}; });
}

class ClientListener { }
const stubs = {
  '../../view/formView': { FormView: { isDisplayingNicknames: false } },
  '../events/queryKeyCodeBindings': {},
  './clientListener': { ClientListener: ClientListener },
  'skyrimPlatform': {
    DxScanCode: { F1: 59, F2: 60, F6: 64, Enter: 28, Escape: 1 },
    Menu: {
      Barter: 'BarterMenu', Book: 'Book Menu', Container: 'ContainerMenu', Crafting: 'Crafting Menu',
      Gift: 'GiftMenu', Inventory: 'InventoryMenu', Journal: 'Journal Menu', Lockpicking: 'Lockpicking Menu',
      Loading: 'Loading Menu', Map: 'MapMenu', RaceSex: 'RaceSex Menu', Stats: 'StatsMenu',
      Tween: 'TweenMenu', Console: 'Console', Main: 'Main Menu', HUD: 'HUD Menu',
    },
  },
};
const BrowserService = load(BROWSER, stubs).BrowserService;


function rig() {
  const w = { calls: [], shown: true, focused: true, handlers: {}, keys: [] };
  const sp = {
    settings: { 'skymp5-client': { gameData: { chooseAtMenu: true, profileId: 7 } } },
    browser: {
      setVisible: function (v) { w.calls.push('visible ' + v); w.shown = v; },
      isVisible: function () { return w.shown; },
      setFocused: function (v) { w.calls.push('focused ' + v); w.focused = v; },
      isFocused: function () { return w.focused; },
      executeJavaScript: function () { },
    },
  };
  const controller = {
    on: function (ev, fn) { w.handlers[ev] = fn; },
    once: function () { },
    emitter: { on: function (ev, fn) { if (ev === 'queryKeyCodeBindings') w.keys.push(fn); }, emit: function () { } },
  };
  w.service = new BrowserService(sp, controller);
  w.open = function (name) { w.handlers.menuOpen({ name: name }); };
  w.close = function (name) { w.handlers.menuClose({ name: name }); };
  w.key = function (code) { w.keys.forEach(function (f) { f({ isDown: function (k) { return k.length === 1 && k[0] === code; } }); }); };
  w.clear = function () { w.calls.length = 0; };
  return w;
}
const KEY = stubs.skyrimPlatform.DxScanCode;

// Held: the choice is up.
globalThis.__thornswoodChoiceHeld = true;
const a = rig();
a.open('Main Menu');
yes('held: the main menu opening leaves the page on screen', a.calls.length === 0 && a.shown === true);
a.close('HUD Menu');
yes('held: the HUD closing leaves the page on screen', a.calls.length === 0 && a.shown === true);
a.key(KEY.Escape);
yes('held: Escape leaves the page its mouse', a.calls.length === 0 && a.focused === true);
a.key(KEY.F6);
yes('held: F6 leaves the page its mouse', a.calls.length === 0 && a.focused === true);
a.key(KEY.F2);
yes('held: F2 leaves the page on screen', a.calls.length === 0 && a.shown === true);
a.open('Console');
yes('held: the console still hides the page', a.shown === false);
a.clear();
a.focused = false;  // NirnLab drops the real focus on a hide
a.close('Console');
yes('held: the console closing shows the page again', a.shown === true);
yes('held: and gives it the mouse back', a.focused === true && a.calls.indexOf('focused true') >= 0);
yes('held: the main menu was never kept as a menu to wait out', a.calls[0] === 'visible true');

// Not held: the world is up, or the pick went in. As before.
globalThis.__thornswoodChoiceHeld = false;
const b = rig();
b.open('Main Menu');
yes('not held: the main menu hides the page, as before', b.shown === false);
b.open('Console');
b.close('Console');
yes('not held: the console closing does not show it while the main menu is open, as before', b.shown === false);
b.close('Main Menu');
yes('not held: the main menu closing shows it, as before', b.shown === true);
b.clear();
b.close('HUD Menu');
yes('not held: the HUD closing hides it, as before', b.shown === false);
b.focused = true;
b.key(KEY.Escape);
yes('not held: Escape takes the mouse off the page, as before', b.focused === false);
b.key(KEY.F6);
yes('not held: F6 gives it back, as before', b.focused === true);
b.open('Console');
b.focused = false;
b.close('Console');
yes('not held: the console closing does not raise focus', b.focused === false);

// Missing: a front from before this change says nothing, which is not held.
delete globalThis.__thornswoodChoiceHeld;
const c = rig();
c.open('Main Menu');
yes('unsaid: the main menu hides the page, as before', c.shown === false);

console.log('');
console.log(pass + ' passed, ' + fail + ' failed');
process.exit(fail ? 1 : 0);
