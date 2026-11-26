// test-game-year.js
//
//     node skymp5-client/test-game-year.js
//
// Thornswood #3156. The map shows the game's date, and its year said 4E 205 in
// 2026: TimeService wrote the year as the real one minus 2020 plus 199, which
// is upstream's count. Thornswood's world is 4E 252 in 2026 (its lore/README.md),
// so the year is now the real one minus 2026 plus 252, and it still turns with
// the real year, as the day and the month turn with the real day.
//
// WHAT THIS RUNS. The real timeService.ts, with its TypeScript types taken off
// (node's own module.stripTypeScriptTypes, Node 22.13 or newer, or the
// typescript package when it is installed), over a stand-in game whose
// calendar globals record what they are set to, at a clock turned by hand.
//
// Run it against the code before the change with TIMESERVICE=<that file>: the
// year cases fail there.
'use strict';

const fs = require('fs');
const path = require('path');

const ROOT = __dirname;
const TIMESERVICE = process.env.TIMESERVICE ||
  path.join(ROOT, 'src', 'services', 'services', 'timeService.ts');

let pass = 0;
let fail = 0;
function yes(what, cond) {
  if (cond) {
    pass++;
    console.log('PASS  ' + what);
  } else {
    fail++;
    console.log('FAIL  ' + what);
  }
}

// TypeScript to JavaScript, types only, as test-update-churn.js does it.
function stripTypes(src) {
  const mod = require('module');
  if (typeof mod.stripTypeScriptTypes === 'function') {
    const warn = process.emitWarning;
    process.emitWarning = function () { };
    try {
      return mod.stripTypeScriptTypes(src, { mode: 'transform' });
    } finally {
      process.emitWarning = warn;
    }
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

// Include elapsed days to check that date correction leaves them alone (#4661).
function rig(hoursOffset, initial) {
  const globals = Object.assign({ 0x35: 0, 0x36: 0, 0x37: 0, 0x38: 0, 0x39: 12345.5, 0x3a: 20 }, initial);
  const writes = [];
  const sp = {
    settings: { 'skymp5-client': hoursOffset === undefined ? {} : { hoursOffset: hoursOffset } },
    Game: { getFormEx: function (id) { return Object.prototype.hasOwnProperty.call(globals, id) ? { id: id } : null; } },
    GlobalVariable: {
      from: function (f) {
        if (!f) { return null; }
        return {
          getValue: function () { return globals[f.id]; },
          setValue: function (v) { writes.push(f.id); globals[f.id] = v; },
        };
      },
    },
  };
  let onUpdate = null;
  const controller = { on: function (ev, fn) { if (ev === 'update') { onUpdate = fn; } } };
  const mod = load(TIMESERVICE, { './clientListener': { ClientListener: class { } } });
  const service = new mod.TimeService(sp, controller);
  return {
    globals: globals,
    writes: writes,
    // One update at the given UTC time, as the game would run it.
    at: function (iso) {
      const real = Date.now;
      const t = Date.parse(iso);
      Date.now = function () { return t; };
      try {
        service.lastTimeUpd = 0;
        onUpdate();
      } finally {
        Date.now = real;
      }
      return globals;
    },
  };
}

let r = null;
try { r = rig(); } catch (e) { console.log('could not load timeService.ts: ' + e); }
yes('timeService.ts loads and listens for update', !!r);

if (r) {
  const g = r.at('2026-10-26T15:46:00Z');
  yes('26 October 2026 is the 26th of Frostfall, 4E 252',
    g[0x37] === 26 && g[0x36] === 9 && g[0x35] === 252);
  yes('the hour is the real one in UTC', Math.abs(g[0x38] - (15 + 46 / 60)) < 1e-9);

  const n = rig().at('2027-01-01T00:30:00Z');
  yes('the year turns with the real year: 1 January 2027 is the 1st of Morning Star, 4E 253',
    n[0x37] === 1 && n[0x36] === 0 && n[0x35] === 253);

  const e = rig().at('2026-12-31T23:59:00Z');
  yes('the last minute of 2026 is still 4E 252, the 31st of Evening Star',
    e[0x37] === 31 && e[0x36] === 11 && e[0x35] === 252);

  // hoursOffset moves the whole date, so a server that sets one has the year
  // turn at its own midnight, with the day and the month.
  const o = rig(2).at('2026-12-31T23:00:00Z');
  yes('an hoursOffset of 2 turns the year with the day: 23:00 UTC on 31 December is 4E 253',
    o[0x37] === 1 && o[0x36] === 0 && o[0x35] === 253);

  // An already matching hour must not keep a saved world's old date (#4661).
  const hour = 12 + 34 / 60;
  const matching = rig(0, { 0x38: hour, 0x37: 1, 0x36: 0, 0x35: 201 });
  const m = matching.at('2026-11-26T12:34:00Z');
  yes('a matching hour still corrects a stale day, month and year',
    m[0x37] === 26 && m[0x36] === 10 && m[0x35] === 252);
  yes('matching-hour date correction leaves the hour and elapsed days alone',
    !matching.writes.includes(0x38) && !matching.writes.includes(0x39) && m[0x39] === 12345.5);

  for (const seconds of [-30, 30]) {
    const drift = rig(0, { 0x38: hour + seconds / 3600, 0x37: 25, 0x36: 10, 0x35: 252 });
    const d = drift.at('2026-11-26T12:34:00Z');
    yes('sub-minute hour drift still corrects the day: ' + seconds,
      d[0x37] === 26);
    yes('sub-minute drift keeps the hour threshold and TimeScale direction: ' + seconds,
      d[0x38] === hour + seconds / 3600 && !drift.writes.includes(0x38) &&
      d[0x3a] === (seconds > 0 ? 0.6 : 1.2));
    yes('sub-minute correction writes no already-correct month or year: ' + seconds,
      !drift.writes.includes(0x36) && !drift.writes.includes(0x35));
  }

  const midnight = rig(0, { 0x38: 0, 0x37: 26, 0x36: 10, 0x35: 252 }).at('2026-11-27T00:00:00Z');
  yes('midnight corrects the previous day even when the hour is already zero',
    midnight[0x37] === 27 && midnight[0x36] === 10 && midnight[0x35] === 252);
  const newYear = rig(0, { 0x38: 0, 0x37: 31, 0x36: 11, 0x35: 252 }).at('2027-01-01T00:00:00Z');
  yes('midnight at new year corrects all three date fields with a matching hour',
    newYear[0x37] === 1 && newYear[0x36] === 0 && newYear[0x35] === 253);
  const offset = rig(2, { 0x38: 1, 0x37: 31, 0x36: 11, 0x35: 252 }).at('2026-12-31T23:00:00Z');
  yes('matching-hour correction uses the existing offset across the year boundary',
    offset[0x37] === 1 && offset[0x36] === 0 && offset[0x35] === 253);

  const correct = rig(0, { 0x38: hour, 0x37: 26, 0x36: 10, 0x35: 252 });
  correct.at('2026-11-26T12:34:00Z');
  yes('an already-correct calendar writes only the existing TimeScale adjustment',
    JSON.stringify(correct.writes) === JSON.stringify([0x3a]) && correct.globals[0x3a] === 1.2);
}

console.log('');
console.log(pass + ' passed, ' + fail + ' failed');
process.exit(fail ? 1 : 0);
