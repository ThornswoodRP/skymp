// Thornswood #4598. A press on a base the server answers waits for the server.
//
//     node misc/tests/node/test_server_first_bases.js
//
// dealWithRef (skymp5-client/src/extensions/objectReferenceEx.ts) blocked a
// press by record type alone: containers, items, actors, doors, plants, trees
// and benches. A chopping block is furniture that is no bench, and an ore vein
// is an activator, so both were left to the presser's own Skyrim, which sat
// them at the block or started the swing before the server had said whether
// they may. The server now names the bases it answers in a customPacket
// (customPacketType "serverFirstBases", bases: runtime form ids), and the
// client blocks every reference of those bases.
//
// WHAT THIS RUNS. The real objectReferenceEx.ts and activationService.ts,
// their TypeScript types taken off (node's module.stripTypeScriptTypes, Node
// 22.13 or newer, or the typescript package), over stand-in references.
//
//   1. a chopping block and a vein streamed with no list are left to Skyrim,
//      as before (a chair and a lever stay so throughout);
//   2. the packet blocks the block and the vein already streamed;
//   3. one streamed after the packet is blocked as it streams;
//   4. a chair, a lever, a bench, a door and a locked chest are as before;
//   5. a later list without a base hands its references back to Skyrim;
//   6. a packet of another type, or a list that is not numbers, changes nothing;
//   7. the press on a blocked block is still sent to the server.
'use strict';

const fs = require('fs');
const path = require('path');

const ROOT = path.join(__dirname, '..', '..', '..');
const EX = path.join(ROOT, 'skymp5-client', 'src', 'extensions', 'objectReferenceEx.ts');
const ACT = path.join(ROOT, 'skymp5-client', 'src', 'services', 'services', 'activationService.ts');

let pass = 0;
let fail = 0;
function yes(what, cond) {
  if (cond) { pass++; console.log('PASS  ' + what); }
  else { fail++; console.log('FAIL  ' + what); }
}

// The client's own TypeScript when it is installed (skymp5-client/node_modules
// after the build's yarn install), else node's. activationService.ts has
// constructor parameter properties, which only a transform takes off: node 22
// and 23 have mode 'transform', and from node 24 there is no such mode, so
// there the typescript package is the only way.
const tsMissing = [];
function typescript() {
  const tries = [['require', function () { return require('typescript'); }]];
  [path.join(ROOT, 'skymp5-client'), ROOT].forEach(function (p) {
    tries.push([p, function () { return require(require.resolve('typescript', { paths: [p] })); }]);
  });
  for (const t of tries) {
    try { return t[1](); } catch (e) { tsMissing.push(t[0] + ': ' + e.code); }
  }
  return null;
}

function stripTypes(src) {
  const ts = typescript();
  if (ts && typeof ts.transpileModule === 'function') {
    return ts.transpileModule(src, {
      compilerOptions: { target: ts.ScriptTarget.ES2019, module: ts.ModuleKind.ESNext },
    }).outputText;
  }
  const mod = require('module');
  if (typeof mod.stripTypeScriptTypes === 'function') {
    const warn = process.emitWarning;
    process.emitWarning = function () { };
    try { return mod.stripTypeScriptTypes(src, { mode: 'transform' }); }
    finally { process.emitWarning = warn; }
  }
  throw new Error('needs the typescript package (yarn install in skymp5-client) or node 22.13 to 23; ' +
    'typescript was not found at ' + tsMissing.join(', '));
}

function load(file, stubs) {
  let js = stripTypes(fs.readFileSync(file, 'utf8').split('\r').join(''));
  js = js.replace(/^import\s+\*\s+as\s+(\w+)\s+from\s+["']([^"']+)["'];?/gm, function (m, name, from) {
    return 'const ' + name + ' = __stub(' + JSON.stringify(from) + ');';
  });
  js = js.replace(/^import\s+\{([^}]*)\}\s+from\s+["']([^"']+)["'];?/gm, function (m, names, from) {
    const list = names.split(',').map(function (n) { return n.trim(); }).filter(Boolean)
      .map(function (n) { return n.replace(/^type\s+/, '').replace(/\s+as\s+/, ': '); });
    return 'const { ' + list.join(', ') + ' } = __stub(' + JSON.stringify(from) + ');';
  });
  const exported = [];
  js = js.replace(/^export\s+(class|const|let|var|function)\s+(\w+)/gm, function (m, kind, name) {
    exported.push(name);
    return kind + ' ' + name;
  });
  js += '\nreturn { ' + exported.join(', ') + ' };';
  return new Function('__stub', js)(function (from) { return stubs[from] || {}; });
}

// The stand-in game.
const FormType = { Container: 28, Door: 29, Activator: 24, Furniture: 40, NPC: 43, Flora: 39, Tree: 38, Misc: 32 };
const MotionType = { Keyframed: 4 };
const BENCH_TYPE = { 0x2000: 1 }; // a forge
const refs = {};
const bases = {};
function base(id, type) {
  const b = { getFormID: function () { return id; }, getType: function () { return type; } };
  bases[id] = b;
  return b;
}
function ref(id, b, locked) {
  const r = {
    id: id, blocked: null, locked: !!locked,
    getFormID: function () { return id; },
    getBaseObject: function () { return b; },
    isLocked: function () { return r.locked; },
    blockActivation: function (v) { r.blocked = v; },
    setMotionType: function () { },
    getOpenState: function () { return 0; },
  };
  refs[id] = r;
  return r;
}
const Game = {
  getFormEx: function (id) { return refs[id] || bases[id] || null; },
  getPlayer: function () { return { getFormID: function () { return 0x14; } }; },
};
const ObjectReference = { from: function (f) { return f && f.blockActivation ? f : null; } };
const sp = {
  getWorkbenchType: function (baseId) { return BENCH_TYPE[baseId] || 0; },
  Game: Game, ObjectReference: ObjectReference,
};
const FormTypeEx = { isItem: function (t) { return t === FormType.Misc; } };

const skyrimPlatform = Object.assign({}, sp, {
  Flora: { from: function () { return null; } }, Form: {}, FormType: FormType, MotionType: MotionType,
});
const Ex = load(EX, {
  skyrimPlatform: skyrimPlatform,
  '../sync/movement': {},
  './formTypeEx': { FormTypeEx: FormTypeEx },
}).ObjectReferenceEx;

const sent = [];
const handlers = {};
const controller = {
  on: function (name, fn) { handlers['on:' + name] = fn; },
  emitter: {
    on: function (name, fn) { handlers[name] = fn; },
    emit: function (name, e) { if (name === 'sendMessage') { sent.push(e.message); } },
  },
  lookupListener: function () { return {}; },
};
const Act = load(ACT, {
  skyrimPlatform: skyrimPlatform,
  './clientListener': { ClientListener: class { } },
  '../../messages': { MsgType: { Activate: 'Activate' } },
  '../../sync/inventory': { getInventory: function () { return {}; } },
  '../../view/worldViewMisc': { localIdToRemoteId: function (id) { return id; } },
  './lastInvService': { LastInvService: function () { } },
  '../../logging': { logError: function () { }, logTrace: function () { } },
  '../../extensions/objectReferenceEx': { ObjectReferenceEx: Ex },
}).ActivationService;
new Act(sp, controller);

function packet(content) {
  const fn = handlers.customPacketMessage;
  if (fn) { fn({ message: { contentJsonDump: JSON.stringify(content) } }); }
}

const BLOCK = base(0x7022e, FormType.Furniture);
const VEIN = base(0x1000a, FormType.Activator);
const CHAIR = base(0x1000b, FormType.Furniture);
const LEVER = base(0x1000c, FormType.Activator);
const FORGE = base(0x2000, FormType.Furniture);
const DOOR = base(0x1000d, FormType.Door);
const CHEST = base(0x1000e, FormType.Container);

const block1 = ref(0xb3949, BLOCK);
const vein1 = ref(0xa001, VEIN);
const chair = ref(0xa002, CHAIR);
const lever = ref(0xa003, LEVER);
const forge = ref(0xa004, FORGE);
const door = ref(0xa005, DOOR);
const chest = ref(0xa006, CHEST, true);
[block1, vein1, chair, lever, forge, door, chest].forEach(function (r) { Ex.dealWithRef(r, r.getBaseObject()); });

yes('1. with no list, a chopping block and a vein are left to Skyrim', block1.blocked === false && vein1.blocked === false);
yes('the activation service listens for the server\'s packets', typeof handlers.customPacketMessage === 'function');

packet({ customPacketType: 'serverFirstBases', bases: [0x7022e, 0x1000a] });
yes('2. the list blocks the chopping block already streamed', block1.blocked === true);
yes('2. and the vein already streamed', vein1.blocked === true);

const block2 = ref(0xb394a, BLOCK);
const vein2 = ref(0xa007, VEIN);
Ex.dealWithRef(block2, BLOCK);
Ex.dealWithRef(vein2, VEIN);
yes('3. a block and a vein streamed after the list are blocked as they stream', block2.blocked === true && vein2.blocked === true);

yes('4. a chair and a lever are still Skyrim\'s', chair.blocked === false && lever.blocked === false);
yes('4. a bench and a door still wait for the server', forge.blocked === true && door.blocked === true);
yes('4. a locked chest is still Skyrim\'s until it is open', chest.blocked === false);

packet({ customPacketType: 'serverFirstBases', bases: [0x7022e] });
yes('5. a later list without the vein hands the veins back to Skyrim', vein1.blocked === false && vein2.blocked === false);
yes('5. and keeps the blocks', block1.blocked === true && block2.blocked === true);

packet({ customPacketType: 'craftMade', bases: [0x1000b] });
packet({ customPacketType: 'serverFirstBases', bases: ['1000b'] });
packet({ customPacketType: 'serverFirstBases', bases: 7 });
yes('6. another packet, or a list that is not numbers, changes nothing', chair.blocked === false && block1.blocked === true);

sent.length = 0;
handlers['on:activate']({
  caster: Game.getPlayer(), target: block1,
});
yes('7. a press on a blocked chopping block is still sent to the server',
  sent.length === 1 && sent[0].t === 'Activate' && sent[0].data.target === 0xb3949);

console.log('');
console.log(pass + ' passed, ' + fail + ' failed');
process.exit(fail ? 1 : 0);
