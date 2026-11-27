'use strict';
// The real activation-routing, input and server-answer handlers. Native
// default processing is counted, not modeled as a particular sitting animation.
const fs = require('node:fs'), path = require('node:path'), vm = require('node:vm');
const root = path.resolve(__dirname, '../../..');
const ts = require(require.resolve('typescript', { paths: [path.join(root, 'skymp5-client')] }));
function load(relative, stubs) {
  const src = fs.readFileSync(path.join(root, relative), 'utf8');
  const js = ts.transpileModule(src, { compilerOptions: { module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2019 } }).outputText;
  const box = { exports: {}, Promise, Map, Set, Date, require(name) { return stubs(name); } };
  vm.runInNewContext(js, box, { filename: relative });
  return box.exports;
}
function fixture(type = 40, bench = false, locked = false) {
  const calls = [], sent = [], handlers = {}, updates = [], waits = [];
  let blocked = false, seated = false;
  const base = { getFormID: () => 0x1000, getType: () => type };
  const player = { getFormID: () => 0x14, getFurnitureReference: () => seated ? ref : null };
  const ref = { getFormID: () => 0x2000, getBaseObject: () => base, isLocked: () => locked,
    blockActivation(value) { blocked = value; }, getOpenState: () => 0, setMotionType() {},
    activate(actor, defaultOnly) { calls.push({ actor: actor.getFormID(), defaultOnly }); seated = true; } };
  const inventory = { entries: [] };
  const sp = {
    storage: { pcInv: inventory }, FormType: { Furniture: 40, Container: 28, Door: 29, NPC: 43, Flora: 39, Tree: 38 }, MotionType: { Keyframed: 4 },
    Game: { getPlayer: () => player, getFormEx: id => id === 0x2000 ? ref : base },
    ObjectReference: { from: r => r }, Flora: { from: () => null }, getWorkbenchType: () => bench ? 1 : 0,
    once(name, fn) { updates.push(fn); }, on() {}, getInventoryQueueFence: () => () => true,
    Utility: { isInMenuMode: () => false, wait: () => new Promise(resolve => waits.push(resolve)), waitMenuMode: () => Promise.resolve() },
    Ui: { isMenuOpen: () => seated }
  };
  const inventoryApi = { getInventory: () => inventory, getDiff: () => ({ entries: [] }), applyInventory() { throw Error('unchanged pack must not be rewritten'); } };
  let Ex;
  function stub(name) {
    if (name === 'skyrimPlatform') return sp;
    if (name.endsWith('/clientListener')) return { ClientListener: class {} };
    if (name.endsWith('/formTypeEx')) return { FormTypeEx: { isItem: () => false } };
    if (name.endsWith('/objectReferenceEx')) return { ObjectReferenceEx: Ex };
    if (name.endsWith('/messages')) return { MsgType: { Activate: 1 } };
    if (name.endsWith('/worldViewMisc')) return { localIdToRemoteId: id => id, remoteIdToLocalId: id => id };
    if (name.endsWith('/inventoryApplyCoordinator')) return {inventoryApplyFence: () => Object.assign(sp.getInventoryQueueFence(), {isCurrent:()=>true})};
    if (name.endsWith('/inventory')) return inventoryApi;
    if (name.endsWith('/equipment')) return { isBadMenuShown: () => false };
    if (name.endsWith('/logging')) return { logTrace() {}, logError() {} };
    return {};
  }
  Ex = load('skymp5-client/src/extensions/objectReferenceEx.ts', stub).ObjectReferenceEx;
  const Act = load('skymp5-client/src/services/services/activationService.ts', stub).ActivationService;
  const Remote = load('skymp5-client/src/services/services/remoteServer.ts', stub).RemoteServer;
  const controller = { on(name, fn) { handlers[name] = fn; }, lookupListener: () => ({}), emitter: {
    on() {}, emit(name, data) { if (name === 'sendMessage') sent.push(data.message); }
  } };
  new Act(sp, controller);
  const remote = Object.create(Remote.prototype);
  Object.assign(remote, { sp, controller, ownerSpawnEpoch: 0 });
  Ex.dealWithRef(ref, base);
  return {
    calls, sent,
    press() {
      // blockActivation controls whether Skyrim executes this physical press.
      if (!blocked) ref.activate(player, false);
      handlers.activate({ caster: player, target: ref });
    },
    answer() { remote.onOpenContainerMessage({ message: { target: 0x2000 } }); },
    async flush() {
      for (let i = 0; i < 12; i++) {
        updates.splice(0).forEach(fn => fn()); waits.splice(0).forEach(fn => fn());
        for (let j = 0; j < 8; j++) await Promise.resolve();
      }
    },
    stand() { seated = false; }, blocked: () => blocked
  };
}
let pass = 0, fail = 0;
function yes(name, condition) { if (condition) { pass++; console.log('PASS ' + name); } else { fail++; console.log('FAIL ' + name); } }
(async () => {
  const seat = fixture(); seat.press();
  yes('an ordinary seat waits for the server before native default processing', seat.blocked() && seat.calls.length === 0);
  yes('the real input handler sends one first activation for that seat', seat.sent.length === 1 && seat.sent[0].data.isSecondActivation === false);
  seat.answer(); await seat.flush();
  yes('one accepted seat press invokes native default processing exactly once', seat.calls.length === 1 && seat.calls[0].defaultOnly === true);
  seat.stand(); await seat.flush();
  yes('leaving the seat sends the normal second activation to release occupancy', seat.sent.length === 2 && seat.sent[1].data.isSecondActivation === true);
  const refused = fixture(); refused.press(); await refused.flush();
  yes('a refused seat press has no native default effect', refused.calls.length === 0);
  const craft = fixture(40, true); craft.press(); craft.answer(); await craft.flush();
  yes('an accepted crafting bench still processes once through the same server answer', craft.calls.length === 1 && craft.calls[0].defaultOnly === true);
  craft.stand(); await craft.flush();
  const lever = fixture(24); lever.press();
  yes('an ordinary activator keeps its existing local default processing', !lever.blocked() && lever.calls.length === 1 && lever.calls[0].defaultOnly === false);
  const locked = fixture(28, false, true); locked.press();
  yes('a locked container stays local and is not announced to the server', !locked.blocked() && locked.calls.length === 1 && locked.sent.length === 0);
  console.log('\n' + pass + ' passed, ' + fail + ' failed'); process.exitCode = fail ? 1 : 0;
})().catch(e => { console.error(e.stack); process.exitCode = 1; });
