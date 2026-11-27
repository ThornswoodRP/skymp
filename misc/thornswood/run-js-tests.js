'use strict';
// misc/thornswood/run-js-tests.js
//
// Runs every skymp5-client/test-*.js and every skymp5-server/tests/*.js,
// each in its own node, and fails when any of them fails. A new test in
// either folder runs on the next pull request without a workflow line of its
// own. One that cannot run on a CI runner has a line in
// misc/thornswood/tests-off-ci.txt saying why, and is left out.
//
// Thornswood #7100. Until 27 Nov 2026 each of these was a workflow step by
// name, and a test nobody added a step for ran nowhere: ten of the twelve
// client tests, and every server test (Thornswood #6971). One of them,
// test-connection-denied.js, had stopped working on 18 Nov and nobody knew.
//
//   node misc/thornswood/run-js-tests.js           every test
//   node misc/thornswood/run-js-tests.js --list    the tests it would run
//
// The client's and the server's node_modules must be installed first (yarn
// install in each), as the workflow job does.
const fs = require('fs');
const path = require('path');
const cp = require('child_process');

const ROOT = path.join(__dirname, '..', '..');
const FOLDERS = [
  { dir: 'skymp5-client', match: /^test-.*\.js$/ },
  { dir: path.join('skymp5-server', 'tests'), match: /\.js$/ },
];

function offCi() {
  const out = new Map();
  const file = path.join(__dirname, 'tests-off-ci.txt');
  for (const line of fs.readFileSync(file, 'utf8').split(/\r?\n/)) {
    const t = line.trim();
    if (!t || t.startsWith('#')) continue;
    const m = /^(\S+)\s+(.*)$/.exec(t);
    out.set(m ? m[1] : t, m ? m[2] : '');
  }
  return out;
}

function tests() {
  const off = offCi();
  const run = [];
  for (const f of FOLDERS) {
    const dir = path.join(ROOT, f.dir);
    for (const name of fs.readdirSync(dir).sort()) {
      if (!f.match.test(name)) continue;
      const rel = path.join(f.dir, name).split(path.sep).join('/');
      if (!off.has(rel)) run.push(rel);
    }
  }
  return run;
}

module.exports = { tests, offCi, FOLDERS };

if (require.main === module) {
  const list = tests();
  if (process.argv.includes('--list')) { for (const t of list) console.log(t); process.exit(0); }
  let failed = 0;
  for (const t of list) {
    const began = Date.now();
    const r = cp.spawnSync(process.execPath, [t], { cwd: ROOT, encoding: 'utf8', timeout: 180000 });
    const secs = ((Date.now() - began) / 1000).toFixed(1);
    const ok = r.status === 0 && !r.error;
    console.log((ok ? 'PASS  ' : 'FAIL  ') + t + '  (' + secs + ' s)');
    if (!ok) {
      failed++;
      const said = ((r.stdout || '') + (r.stderr || '')).split(/\r?\n/);
      for (const l of said.slice(-40)) console.log('      ' + l);
      if (r.error) console.log('      ' + String(r.error.message || r.error));
    }
  }
  console.log('');
  console.log((list.length - failed) + ' passed, ' + failed + ' failed');
  process.exitCode = failed ? 1 : 0;
}
