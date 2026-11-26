'use strict';
// Thornswood #6200: on 26 Nov 2026 the game's menu cursor came up at
// 1720,720 of 3440x1440 at the data load, and the page's first mouse move was
// at 0,0. What carried it there is not proven, so the first mouse read after
// the data load moves nothing and says what it held, and the reads after it
// and the game's acquiring of the mouse are written to the input log. These
// checks read the sources, since the hook needs DirectInput and the game.
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

const trace = state.search(/g_mouseReadsLeft\.fetch_sub\(1\)/);
check('the reads after the data load are written down', trace >= 0 &&
      /read %d after the data load: 0x%08lX, motion %ld,%ld/.test(hook));
check('  as the device gave them, before anything is zeroed or left out',
      trace >= 0 && trace < drop && trace < state.indexOf('minimisedAte++'));
check('  only for a mouse', /DI8DEVTYPE_MOUSE &&\s*g_mouseReadsLeft > 0/.test(state));
check('the game acquiring the mouse and letting go of it are written down',
      /TraceMouseAcquire\(m_pDevice, "acquired", hr\)/.test(hook) &&
      /TraceMouseAcquire\(m_pDevice, "let go of", hr\)/.test(hook) &&
      /DI8DEVTYPE_MOUSE/.test(body(hook, 'static void TraceMouseAcquire(')));
const forgetBody = code(body(hook, 'void DInputHook::ForgetMouseMotion() noexcept'));
check('  and the data load starts both again', /g_mouseReadsLeft = MouseTraceReads;/.test(forgetBody) &&
      /g_mouseAcquiresLeft = MouseTraceAcquires;/.test(forgetBody));

const tilted = code(read('platform_se/skyrim_platform/BrowserApiTilted.cpp'));
const watch = body(tilted, 'void BrowserApiTilted::WatchMenus()');
const forget = watch.indexOf('CEFUtils::DInputHook::ForgetMouseMotion();');
check('WatchMenus, at the data load, has the next mouse read move nothing', forget >= 0);
check('  before the start page takes the focus', forget >= 0 && forget < watch.indexOf('SetFocusedImpl(true)'));
check('  and nothing else forgets it', (code(hook + tilted).match(/ForgetMouseMotion\(\)/g) || []).length === 2);

if (failed) { console.log(failed + ' check(s) failed'); process.exitCode = 1; }
