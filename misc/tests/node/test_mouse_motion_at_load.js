'use strict';
// Thornswood #6200: the mouse's motion made while the game loads its data
// moves no cursor. The mouse is read with GetDeviceState, which gives the
// motion since the last read, and the game reads nothing while it loads, so
// the first read at the main menu carried all of it: on 26 Nov 2026 the
// page's first mouse move was at 0,0, one read after the cursor came up at
// 1720,720 of 3440x1440. These checks read the sources, since the hook needs
// DirectInput and the game.
const fs = require('node:fs'), path = require('node:path');
const src = path.join(__dirname, '../../../skyrim-platform/src');
const read = (f) => fs.readFileSync(path.join(src, f), 'utf8');
let failed = 0;
function check(name, ok) {
  console.log((ok ? 'PASS ' : 'FAIL ') + name);
  if (!ok) failed++;
}
function body(text, signature) {
  const at = text.indexOf(signature);
  if (at < 0) return '';
  const open = text.indexOf('{', at);
  let depth = 0;
  for (let i = open; i < text.length; i++) {
    if (text[i] === '{') depth++;
    else if (text[i] === '}' && --depth === 0) return text.slice(open, i + 1);
  }
  return '';
}
const code = (s) => s.replace(/\/\/[^\n]*/g, '').replace(/\/\*[\s\S]*?\*\//g, '');

const hook = read('tilted/hooks/DInputHook.cpp');
const header = read('tilted/hooks/DInputHook.hpp');
check('DInputHook says it can forget the mouse\'s motion', /static void ForgetMouseMotion\(\) noexcept;/.test(header));
check('  and doing so only marks the next read',
      /g_forgetMouseMotion = true;/.test(code(body(hook, 'void DInputHook::ForgetMouseMotion() noexcept'))));

const state = code(body(hook, 'FakeIDirectInputDevice8A::GetDeviceState('));
const drop = state.search(/g_forgetMouseMotion\.exchange\(false\)/);
const handOff = state.indexOf('ProcessMouseData(mouseState)');
check('GetDeviceState drops the motion of one read once it is marked', drop >= 0 &&
      /motion->lX = motion->lY = motion->lZ = 0;/.test(state));
check('  before the page or the game is handed it', drop >= 0 && handOff > drop);
check('  only for a mouse, and only a read that succeeded and is long enough',
      /GET_DIDEVICE_TYPE\(instanceInfo\.dwDevType\) == DI8DEVTYPE_MOUSE/.test(state) &&
      /outDataLen >= sizeof\(DIMOUSESTATE\)/.test(state) &&
      state.indexOf('if (ret != DI_OK)') >= 0 && state.indexOf('if (ret != DI_OK)') < drop);
check('  keeping its buttons', !/rgbButtons[^;]*= 0/.test(state.slice(drop, handOff)));
check('  and says how much it left out in the input log',
      /\[mouse\] the first read after the game's data loaded held/.test(state));

const tilted = code(read('platform_se/skyrim_platform/BrowserApiTilted.cpp'));
const watch = body(tilted, 'void BrowserApiTilted::WatchMenus()');
const forget = watch.indexOf('CEFUtils::DInputHook::ForgetMouseMotion();');
check('WatchMenus, at the data load, has the next mouse read move nothing', forget >= 0);
check('  before the start page takes the focus', forget >= 0 && forget < watch.indexOf('SetFocusedImpl(true)'));
check('  and nothing else forgets it', (code(hook + tilted).match(/ForgetMouseMotion\(\)/g) || []).length === 2);

if (failed) { console.log(failed + ' check(s) failed'); process.exitCode = 1; }
