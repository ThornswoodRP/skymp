'use strict';
// misc/thornswood/test-every-test-runs.js
//
// Every test file in the engine's Thornswood test folders is run by
// something: a workflow names it, a ctest glob in CMakeLists.txt takes it,
// misc/thornswood/run-js-tests.js takes it, a test that runs names it (the
// C++ a node test builds), or misc/thornswood/tests-off-ci.txt says why no
// runner can.
//
// WHY. Thornswood #7100, 27 Nov 2026. A test in the tree that runs nowhere
// is the check of a fix that nobody hears about when it breaks. Ten of the
// twelve skymp5-client/test-*.js ran nowhere, no skymp5-server test ran
// (Thornswood #6971), and test-connection-denied.js had stopped working on
// 18 Nov, when the client's connect began resetting the server-first
// activation rules, without anybody knowing.
//
// It also fails on a tests-off-ci.txt line that names no file, has no reason,
// or names a test that something does run (the line has gone stale).
//
// Read only.
const fs = require('fs');
const path = require('path');
const runner = require('./run-js-tests.js');

const ROOT = path.join(__dirname, '..', '..');
let pass = 0, fail = 0;
function yes(what, cond) {
  if (cond) { pass++; console.log('PASS  ' + what); } else { fail++; console.log('FAIL  ' + what); }
}
const rel = p => path.relative(ROOT, p).split(path.sep).join('/');
function walk(dir, out) {
  if (!fs.existsSync(dir)) return out;
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, e.name);
    if (e.isDirectory()) { if (e.name !== 'node_modules') walk(p, out); } else out.push(rel(p));
  }
  return out;
}

// The test files: what is named as a test in the folders Thornswood writes
// its engine tests in.
const isTest = f =>
  /^skymp5-client\/test-[^/]+\.js$/.test(f) ||
  /^skymp5-server\/tests\/[^/]+\.js$/.test(f) ||
  /^misc\/tests\/(?:.*\/)?test[_-][^/]+\.(?:js|cpp|ps1)$/.test(f) ||
  /^misc\/thornswood\/test-[^/]+\.(?:js|ps1)$/.test(f);
const all = []
  .concat(walk(path.join(ROOT, 'skymp5-client'), []).filter(f => /^skymp5-client\/[^/]+$/.test(f)))
  .concat(walk(path.join(ROOT, 'skymp5-server', 'tests'), []))
  .concat(walk(path.join(ROOT, 'misc', 'tests'), []))
  .concat(walk(path.join(ROOT, 'misc', 'thornswood'), []))
  .filter(isTest).sort();
yes('there are test files to account for, ' + all.length + ' of them', all.length > 50);

// What runs them.
const wfDir = path.join(ROOT, '.github', 'workflows');
const workflows = fs.readdirSync(wfDir).filter(f => /\.ya?ml$/.test(f))
  .map(f => fs.readFileSync(path.join(wfDir, f), 'utf8')).join('\n');
const cmake = fs.readFileSync(path.join(ROOT, 'CMakeLists.txt'), 'utf8');
const globs = [];
for (const m of cmake.matchAll(/file\(GLOB\s+\w+\s+"\$\{CMAKE_SOURCE_DIR\}\/([^"]+)"\)/g)) {
  globs.push(new RegExp('^' + m[1].replace(/[.+^${}()|[\]\\]/g, '\\$&').replace(/\*/g, '[^/]*') + '$'));
}
yes('CMakeLists.txt still globs misc/tests/node/test_*.js for ctest',
    globs.some(g => g.test('misc/tests/node/test_x.js')));
const byRunner = new Set(runner.tests());
const off = runner.offCi();
const name = f => path.basename(f).replace(/\.[^.]+$/, '');

function runBy(f, seen) {
  if (workflows.includes(f) || workflows.includes(f.split('/').join('\\'))) return 'a workflow';
  if (globs.some(g => g.test(f))) return 'a ctest glob';
  if (byRunner.has(f)) return 'run-js-tests.js';
  if (f === 'misc/thornswood/test-every-test-runs.js' && workflows.includes('test-every-test-runs.js')) return 'a workflow';
  return null;
}
const runs = new Map();
for (const f of all) { const by = runBy(f); if (by) runs.set(f, by); }
// A test another test runs: named by it (a node test building a .cpp, a
// .ps1 building one), when that other test is run or is off CI with reason.
let grew = true;
while (grew) {
  grew = false;
  for (const f of all) {
    if (runs.has(f) || off.has(f)) continue;
    for (const g of all) {
      if (g === f || !(runs.has(g) || off.has(g))) continue;
      const text = fs.readFileSync(path.join(ROOT, g), 'utf8');
      if (text.includes(path.basename(f)) || new RegExp('(^|[^A-Za-z0-9_-])' + name(f) + '([^A-Za-z0-9_-]|$)').test(text)) {
        runs.set(f, (runs.has(g) ? '' : 'by hand, ') + 'through ' + g);
        grew = true;
        break;
      }
    }
  }
}

const nowhere = all.filter(f => !runs.has(f) && !off.has(f));
for (const f of nowhere) console.log('      runs nowhere: ' + f);
yes('every test file is run by CI, or misc/thornswood/tests-off-ci.txt says why not (' + nowhere.length + ' run nowhere)',
    nowhere.length === 0);

const badOff = [];
for (const [f, why] of off) {
  if (!fs.existsSync(path.join(ROOT, f))) badOff.push(f + ' is not there');
  else if (!why) badOff.push(f + ' gives no reason');
  else if (runBy(f)) badOff.push(f + ' is run by ' + runBy(f) + ', so its line is stale');
}
for (const b of badOff) console.log('      tests-off-ci.txt: ' + b);
yes('every tests-off-ci.txt line names a test that is there, says why, and is not run anyway', badOff.length === 0);

yes('run-js-tests.js takes the client tests and the server tests', byRunner.has('skymp5-client/test-connection-denied.js') &&
    [...byRunner].some(f => f.startsWith('skymp5-server/tests/')));

console.log('');
console.log(pass + ' passed, ' + fail + ' failed');
process.exitCode = fail ? 1 : 0;
