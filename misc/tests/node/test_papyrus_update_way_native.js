'use strict';
// Thornswood #6201. The 'update' event's Papyrus call, its way opened by the
// call's own end. Builds test_papyrus_update_way_native.cpp against the real
// PapyrusUpdateWay.h and runs it, then reads PapyrusTESModPlatform.cpp, whose
// functions need the game, for how it uses the way.
//
// 15 Nov 2026, main build-1.3.69, 2:47 PM: the server made a new character in
// the creation hall and the game loaded the hall. For the next minute 'tick'
// ran and 'update' never did: no load order check, no join pass, no race
// menu, while the game still drew and moved. TESModPlatform::Update runs after
// every 'tick', and sent no call because its way was shut: only Add's return
// and a load event opened it, and the call on its way never reached Add.
const fs = require('node:fs'), path = require('node:path'), os = require('node:os'), cp = require('node:child_process');
const headers = path.join(__dirname, '../../../skyrim-platform/src/platform_se/skyrim_platform');
if (!fs.existsSync(path.join(headers, 'PapyrusUpdateWay.h'))) throw Error('PapyrusUpdateWay.h missing');

let failed = 0;
function check(name, ok) {
  console.log((ok ? 'PASS ' : 'FAIL ') + name);
  if (!ok) failed++;
}
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

const src = code(fs.readFileSync(path.join(headers, 'PapyrusTESModPlatform.cpp'), 'utf8'));
check('no load event opens the way any more', !/TESLoadGameEvent|vmCallAllowed/.test(src));
const functor = body(src, 'class StackCallbackFunctor');
check('the callback ends the call when the VM runs it',
      !!functor && /void operator\(\)\(Variable a_result\) override\s*\{[^}]*End\(\)/.test(functor));
check('and when the VM lets go of it', !!functor && /~StackCallbackFunctor\(\) override\s*\{\s*End\(\);\s*\}/.test(functor));
check('ending it opens the way for that call only', !!functor && /updateWay\.End\(this,/.test(functor));
const add = body(src, 'int32_t TESModPlatform::Add(');
check('Add does not open the way itself', !!add && !/updateWay/.test(add));
const update = body(src, 'void TESModPlatform::Update()');
check('Update sends only while the way is open', !!update && /if \(!updateWay\.Open\(/.test(update));
const send = update ? update.indexOf('updateWay.Send(call') : -1;
const dispatch = update ? update.indexOf('DispatchStaticCall(') : -1;
check('Update puts the call on its way before it reaches the VM', send >= 0 && dispatch > send);

const work = fs.mkdtempSync(path.join(os.tmpdir(), 'thornswood-native-update-way-'));
try {
  const fixture = path.join(__dirname, 'test_papyrus_update_way_native.cpp');
  const exe = path.join(work, process.platform === 'win32' ? 'way.exe' : 'way');
  const {command: compiler, env} = require('./cxx').compiler();
  const args = process.platform === 'win32'
    ? ['/nologo', '/EHsc', '/std:c++17', '/I' + headers, '/Fe:' + exe, '/Fo:' + path.join(work, 'way.obj'), fixture]
    : ['-std=c++17', '-I' + headers, fixture, '-o', exe];
  const build = cp.spawnSync(compiler, args, {encoding: 'utf8', cwd: work, env});
  process.stdout.write(build.stdout || ''); process.stderr.write(build.stderr || '');
  if (build.error) throw build.error;
  if (build.status !== 0) failed++;
  else {
    const run = cp.spawnSync(exe, [], {encoding: 'utf8'});
    process.stdout.write(run.stdout || ''); process.stderr.write(run.stderr || '');
    if (run.status !== 0) failed++;
  }
} finally {
  const parent = fs.realpathSync(os.tmpdir());
  const target = fs.realpathSync(work);
  if (path.dirname(target).toLowerCase() !== parent.toLowerCase() || !path.basename(target).startsWith('thornswood-native-update-way-')) throw Error('Unsafe test cleanup target');
  fs.rmSync(target, {recursive: true, force: true});
}
if (failed) process.exitCode = 1;
