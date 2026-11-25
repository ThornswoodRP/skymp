'use strict';
// Thornswood #4609. Run the real Papyrus transform writers against two
// stand-in subscribers that have already received the board.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const root = path.resolve(__dirname, '../../..');
const source = fs.readFileSync(path.join(root,
  'skymp5-server/cpp/server_guest_lib/script_classes/PapyrusObjectReference.cpp'), 'utf8');
function extract(name, next) {
  const start = source.indexOf('VarValue PapyrusObjectReference::' + name + '(');
  const end = source.indexOf('VarValue PapyrusObjectReference::' + next + '(', start);
  assert(start >= 0 && end > start);
  return source.slice(start, end);
}
const work = fs.mkdtempSync(path.join(os.tmpdir(), 'placed-transform-'));
try {
  fs.writeFileSync(path.join(work, 'under-test.inc'), extract('SetAngle', 'Enable') + extract('MoveTo', 'SetOpen'));
  const { command, env } = require('./cxx').compiler();
  const exe = path.join(work, 'test.exe');
  const fixture = path.join(__dirname, 'test_placed_object_transform.cpp');
  const args = process.platform === 'win32'
    ? ['/nologo', '/EHsc', '/std:c++17', '/I' + work, '/Fe:' + exe, '/Fo:' + work + path.sep, fixture]
    : ['-std=c++17', '-I' + work, '-o', exe, fixture];
  const built = spawnSync(command, args, { env, stdio: 'inherit' });
  if (built.error) throw built.error;
  if (built.status !== 0) throw new Error('fixture compilation failed');
  const run = spawnSync(exe, [], { stdio: 'inherit' });
  if (run.error) throw run.error;
  process.exitCode = run.status ?? 1;
} finally {
  fs.rmSync(work, { recursive: true, force: true });
}
