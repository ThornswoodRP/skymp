'use strict';
// Thornswood #6886. The person's own character controller state, written to
// the platform log when it changes. Builds test_character_state_log_native.cpp
// against the real CharacterStateLog.h and runs it, then reads the files that
// use it, which need the game, for how they do.
//
// 25 Nov 2026, main: in first person looking up and walking forward lifted the
// character and looking down lowered it. Nothing in the session logs said
// which state the game's controller was in.
const fs = require('node:fs'), path = require('node:path'),
      os = require('node:os'), cp = require('node:child_process');
const headers = path.join(
  __dirname, '../../../skyrim-platform/src/platform_se/skyrim_platform');
if (!fs.existsSync(path.join(headers, 'CharacterStateLog.h')))
  throw Error('CharacterStateLog.h missing');

let failed = 0;
function check(name, ok)
{
  console.log((ok ? 'PASS ' : 'FAIL ') + name);
  if (!ok)
    failed++;
}
const code = (s) =>
  s.replace(/\/\/[^\n]*/g, '').replace(/\/\*[\s\S]*?\*\//g, '');

const tick =
  code(fs.readFileSync(path.join(headers, 'TickHandler.h'), 'utf8'));
check('every tick writes the movement state',
      /LogPlayerMovement\(\);/.test(tick));
const log =
  code(fs.readFileSync(path.join(headers, 'PlayerMovementLog.cpp'), 'utf8'));
check('it reads the controller\'s state, wanted state and flags',
      /context\.currentState/.test(log) && /wantState/.test(log) &&
        /flags\.underlying\(\)/.test(log));
check('it writes only what CharacterStateLog lets through',
      /if \(!g_log\.Write\(/.test(log));
check(
  'it writes nothing to the character, its controller or the camera',
  !/->\s*(context\.currentState|wantState|flags|pitchAngle|waterHeight)\s*=[^=]/
      .test(log) &&
    !/\b(Set|Force|Enable|Disable)[A-Z]\w*\(/.test(log));

const work =
  fs.mkdtempSync(path.join(os.tmpdir(), 'thornswood-native-character-state-'));
try {
  const fixture = path.join(__dirname, 'test_character_state_log_native.cpp');
  const exe =
    path.join(work, process.platform === 'win32' ? 'state.exe' : 'state');
  const { command : compiler, env } = require('./cxx').compiler();
  const args = process.platform === 'win32'
    ? ['/nologo', '/EHsc', '/std:c++17', '/I' + headers, '/Fe:' + exe, '/Fo:' + path.join(work, 'state.obj'), fixture]
    : ['-std=c++17', '-I' + headers, fixture, '-o', exe];
  const build =
    cp.spawnSync(compiler, args, { encoding : 'utf8', cwd : work, env });
  process.stdout.write(build.stdout || '');
  process.stderr.write(build.stderr || '');
  if (build.error)
    throw build.error;
  if (build.status !== 0)
    failed++;
  else {
    const run = cp.spawnSync(exe, [], { encoding : 'utf8' });
    process.stdout.write(run.stdout || '');
    process.stderr.write(run.stderr || '');
    if (run.status !== 0)
      failed++;
  }
} finally {
  const parent = fs.realpathSync(os.tmpdir());
  const target = fs.realpathSync(work);
  if (path.dirname(target).toLowerCase() !== parent.toLowerCase() ||
      !path.basename(target).startsWith('thornswood-native-character-state-'))
    throw Error('Unsafe test cleanup target');
  fs.rmSync(target, { recursive : true, force : true });
}
if (failed)
  process.exitCode = 1;
