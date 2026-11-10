'use strict';
// Thornswood #4523. Skyrim Platform answers which window has the keyboard.
//
// The start screen's guard asks win32.foregroundWindow() and ignores keys
// while another program is in front. The engine shipped without it, so the
// guard never turned on, and the client's own test passed on a stand-in that
// had it. This fails when the engine lacks it: win32 has to register
// foregroundWindow, the declarations have to carry it, and the answer has to
// set every field the client reads. Then it builds
// test_foreground_window_native.cpp against the real ForegroundWindow.h and
// runs it.
const fs = require('node:fs'), path = require('node:path'), os = require('node:os'), cp = require('node:child_process');
const ROOT = path.join(__dirname, '../../..');
const SP = path.join(ROOT, 'skyrim-platform/src/platform_se/skyrim_platform');
const read = (p) => fs.readFileSync(path.join(ROOT, p), 'utf8').split('\r').join('');

let failed = 0;
function yes(what, cond) {
  console.log((cond ? 'PASS  ' : 'FAIL  ') + what);
  if (!cond) { failed++; }
}

const header = read('skyrim-platform/src/platform_se/skyrim_platform/Win32Api.h');
yes('win32 registers foregroundWindow',
    /win32\.Set\(\s*"foregroundWindow",[\s\S]*?return ForegroundWindow\(info\);/.test(header));

const source = read('skyrim-platform/src/platform_se/skyrim_platform/Win32Api.cpp');
const body = (source.match(/Napi::Value Win32Api::ForegroundWindow\(const Napi::CallbackInfo& info\)\n\{([\s\S]*?)\n\}\n/) || [])[1] || '';
yes('it is written', body !== '');
yes('it asks Windows which window is in front, and answers null for none',
    /GetForegroundWindow\(\)/.test(body) && /if \(!window\) \{\s*return env\.Null\(\);/.test(body));
yes('it judges the window by its process against the game\'s own',
    /GetWindowThreadProcessId\(window, &pid\)/.test(body) && /GetCurrentProcessId\(\)/.test(body));
for (const field of ['isGame', 'ourProcess', 'exe', 'title', 'className']) {
  yes('the answer sets ' + field + ', which the client reads', body.indexOf('res.Set("' + field + '"') >= 0);
}

for (const file of ['skyrim-platform/src/platform_se/codegen/convert-files/Definitions.txt',
                    'skyrim-platform/src/platform_se/codegen/convert-files/skyrimPlatform.ts']) {
  const text = read(file);
  yes(file + ' declares win32.foregroundWindow',
      /export interface Win32 \{[^}]*foregroundWindow\(\): ForegroundWindow \| null[^}]*\}/.test(text) &&
      /export interface ForegroundWindow \{[^}]*isGame: boolean[^}]*\}/.test(text));
}

if (!fs.existsSync(path.join(SP, 'ForegroundWindow.h'))) {
  yes('ForegroundWindow.h is there to build', false);
} else {
  const work = fs.mkdtempSync(path.join(os.tmpdir(), 'thornswood-native-foreground-'));
  try {
    const fixture = path.join(__dirname, 'test_foreground_window_native.cpp');
    const exe = path.join(work, process.platform === 'win32' ? 'foreground.exe' : 'foreground');
    const {command: compiler, env} = require('./cxx').compiler();
    const args = process.platform === 'win32'
      ? ['/nologo', '/EHsc', '/std:c++17', '/I' + SP, '/Fe:' + exe, '/Fo:' + path.join(work, 'foreground.obj'), fixture]
      : ['-std=c++17', '-I' + SP, fixture, '-o', exe];
    const build = cp.spawnSync(compiler, args, {encoding: 'utf8', cwd: work, env});
    process.stdout.write(build.stdout || ''); process.stderr.write(build.stderr || '');
    if (build.error) throw build.error;
    if (build.status !== 0) { failed++; }
    else {
      const run = cp.spawnSync(exe, [], {encoding: 'utf8'});
      process.stdout.write(run.stdout || ''); process.stderr.write(run.stderr || '');
      if (run.status !== 0) { failed++; }
    }
  } finally {
    const parent = fs.realpathSync(os.tmpdir());
    const target = fs.realpathSync(work);
    if (path.dirname(target).toLowerCase() !== parent.toLowerCase() || !path.basename(target).startsWith('thornswood-native-foreground-')) throw Error('Unsafe test cleanup target');
    fs.rmSync(target, {recursive: true, force: true});
  }
}

process.exitCode = failed ? 1 : 0;
