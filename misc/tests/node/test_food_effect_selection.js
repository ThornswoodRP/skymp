'use strict';
// Run the real server consumption callback with stand-in record data.
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const { spawnSync } = require('node:child_process');
const root = path.resolve(__dirname, '../../..');
const source = fs.readFileSync(path.join(root, 'skymp5-server/cpp/server_guest_lib/gamemode_events/EatItemEvent.cpp'), 'utf8');
const start = source.indexOf('void EatItemEvent::OnFireSuccess(');
if (start < 0) throw new Error('consumption callback missing');
const work = fs.mkdtempSync(path.join(os.tmpdir(), 'food-effects-'));
try {
  fs.writeFileSync(path.join(work, 'callback.inc'), source.slice(start));
  const { command, env } = require('./cxx').compiler();
  const fixture = path.join(__dirname, 'test_food_effect_selection.cpp');
  const exe = path.join(work, 'test.exe');
  const include = path.join(root, 'libespm/include');
  const args = process.platform === 'win32'
    ? ['/nologo', '/EHsc', '/std:c++17', '/I' + work, '/I' + include, '/Fe:' + exe, '/Fo:' + work + path.sep, fixture]
    : ['-std=c++17', '-I' + work, '-I' + include, '-o', exe, fixture];
  for (const [cmd, argv] of [[command, args], [exe, []]]) {
    const result = spawnSync(cmd, argv, { env, stdio: 'inherit' });
    if (result.error) throw result.error;
    if (result.status !== 0) { process.exitCode = result.status ?? 1; break; }
  }
} finally {
  fs.rmSync(work, { recursive: true, force: true });
}
