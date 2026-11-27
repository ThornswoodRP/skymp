// test-own-drop.js
//
//     node skymp5-client/test-own-drop.js
//
// Thornswood #6718. Dropping your own clothes in Belethor's shop and picking
// them up again was stealing. The client deletes the game's own drop
// (dropItemService.ts) and the server places a reference of its own
// (MpActor::DropItem), which every client spawns with player.placeAtMe and no
// owner. A reference with no owner takes its cell's, and Belethor owns his
// shop. The game's own drop would have been yours.
//
// The fix: the server keeps who dropped it and says droppedByMe only to that
// character (PartOne.cpp), and their game owns it as soon as it is spawned.
// Everybody else's game keeps it as the place has it.
//
// WHAT THIS RUNS. The real formView.ts with its types stripped, over stand-ins
// for skyrimPlatform and the client's modules. Run it against the code before
// the change with FORMVIEW=<that file>: the first case fails there.
'use strict';

const fs = require('fs');
const path = require('path');

const FORMVIEW = process.env.FORMVIEW || path.join(__dirname, 'src', 'view', 'formView.ts');

let pass = 0;
let fail = 0;
function yes(what, cond) {
  if (cond) { pass++; console.log('PASS  ' + what); } else { fail++; console.log('FAIL  ' + what); }
}

function stripTypes(src) {
  const mod = require('module');
  if (typeof mod.stripTypeScriptTypes === 'function') {
    const warn = process.emitWarning;
    process.emitWarning = function () { };
    try { return mod.stripTypeScriptTypes(src, { mode: 'transform' }); } finally { process.emitWarning = warn; }
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

const MISC = 32;                 // not an NPC: an item on the floor
const DROP = 0xff000a10;          // the server's own id for the dropped item
const CLOTHES = 0x0001be1a;       // a base the dropped reference stands for
const PLAYER_BASE = { formId: 7 };

function rig() {
  const w = { once: [], spawned: [], forms: {}, nextId: 0xff000100 };
  const baseForm = { getFormID: function () { return CLOTHES; }, getType: function () { return MISC; } };
  function refr(id, base) {
    const r = {
      id: id, owner: undefined,
      getFormID: function () { return id; },
      getBaseObject: function () { return base; },
      isDeleted: function () { return false; },
      isDisabled: function () { return false; },
      delete: function () { },
      setAngle: function () { }, setDisplayName: function () { },
      setActorOwner: function (b) { r.owner = b; },
    };
    w.forms[id] = r;
    return r;
  }
  const player = { actor: true, getFormID: function () { return 0x14; }, getActorBase: function () { return PLAYER_BASE; } };
  player.placeAtMe = function (b) { const id = w.nextId++; const r = refr(id, b); w.spawned.push(r); return r; };
  w.forms[0x14] = player;
  w.forms[CLOTHES] = baseForm;
  const sp = {
    once: function (ev, fn) { w.once.push(fn); return {}; },
    printConsole: function () { },
    destroyText: function () { },
    Game: { getPlayer: function () { return player; }, getFormEx: function (id) { return w.forms[id] || null; } },
    ObjectReference: { from: function (f) { return f || null; } },
    Actor: { from: function (f) { return f && f.actor ? f : null; } },
    TESModPlatform: { setWeaponDrawnMode: function () { } },
    FormType: { NPC: 43 },
    storage: {},
    Utility: {},
  };
  const cleaner = { modWcProtection: function () { } };
  const controller = { lookupListener: function () { return cleaner; }, emitter: { emit: function () { } } };
  const stubs = {
    skyrimPlatform: sp,
    './spawnProcess': { SpawnProcess: function () { } },
    './spawnTrace': { traceSpawnChosen: function () { }, traceRespawnNeeded: function () { } },
    '../extensions/objectReferenceEx': { ObjectReferenceEx: { getWorldOrCell: function () { return 0x3c; }, getPos: function () { return [0, 0, 0]; }, dealWithRef: function () { } } },
    '../services/spApiInteractor': { SpApiInteractor: { getControllerInstance: function () { return controller; } } },
  };
  w.FormView = load(FORMVIEW, stubs).FormView;
  w.model = function (extra) {
    return Object.assign({ refrId: DROP, baseId: CLOTHES, movement: { worldOrCell: 0x3c, pos: [1, 2, 3], rot: [0, 0, 0] }, numAppearanceChanges: 0 }, extra || {});
  };
  return w;
}

function run(what, fn) {
  try { fn(); } catch (e) { fail++; console.log('FAIL  ' + what + ': ' + (e && e.stack || e)); }
}

run('your own drop', function () {
  const w = rig();
  const view = new w.FormView(DROP);
  view.update(w.model({ droppedByMe: true }));
  yes('the dropped item is spawned', w.spawned.length === 1);
  yes('and it is yours: owned by your character\'s base, not the shop\'s', w.spawned[0] && w.spawned[0].owner === PLAYER_BASE);
});

run('somebody else\'s drop', function () {
  const w = rig();
  const view = new w.FormView(DROP);
  view.update(w.model());
  yes('somebody else\'s drop is spawned with no owner set, so the place has it as before',
    w.spawned.length === 1 && w.spawned[0].owner === undefined);
});

run('respawned on a new base', function () {
  const w = rig();
  const view = new w.FormView(DROP);
  view.update(w.model({ droppedByMe: true }));
  const other = { getFormID: function () { return 0x0001be1b; }, getType: function () { return MISC; } };
  w.forms[0x0001be1b] = other;
  view.update(w.model({ droppedByMe: true, baseId: 0x0001be1b }));
  yes('a reference spawned again is yours again', w.spawned.length === 2 && w.spawned[1].owner === PLAYER_BASE);
});

console.log('');
console.log(pass + ' passed, ' + fail + ' failed');
process.exit(fail ? 1 : 0);
