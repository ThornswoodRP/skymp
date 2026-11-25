'use strict';
// Run the real furniture handlers against native wrappers that expire on a
// Papyrus update, as SP3NativeValueCasts::NativeObjectToJsObject does. Never
// give the stand-in player or reference stable identity across an await.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const root = path.resolve(__dirname, '../../..');
const ts = require(require.resolve('typescript', {paths: [path.join(root, 'skymp5-client')]}));
const source = fs.readFileSync(path.join(root, 'skymp5-client/src/services/services/remoteServer.ts'), 'utf8');
const js = ts.transpileModule(source, {compilerOptions: {
  module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2018
}}).outputText;

function fixture(options = {}) {
  let frame = 0, seated = false, actual = {entries: []};
  const jobs = [], updates = [], errors = [], opens = [], applies = [], messages = [];
  const state = {target: true, player: true, paused: false};
  const storage = {pcInv: {entries: [{baseId: 100, count: 1}]}};
  if (options.current) actual = storage.pcInv;
  const targetId = 0x250bbf6c;
  const type = options.container ? 28 : 40;

  function native(id, actor = false) {
    const born = frame;
    function valid() {
      if (born !== frame) throw Error('Invalid _skyrimPlatform_indexInPool: fixture wrapper expired at Papyrus update');
    }
    return {
      getFormID() { valid(); return id; },
      getBaseObject() {
        valid();
        return {getType() { valid(); return actor ? 43 : type; }};
      },
      getFurnitureReference() { valid(); return seated ? native(targetId) : null; },
      activate(player, defaultOnly) {
        valid(); assert.equal(player.getFormID(), 0x14); assert.equal(defaultOnly, true);
        opens.push(frame); seated = true;
      }
    };
  }
  const sp = {
    storage, FormType: {Furniture: 40, Container: 28},
    Game: {
      getPlayer: () => state.player ? native(0x14, true) : null,
      getFormEx: id => state.target ? native(id) : null
    },
    ObjectReference: {from: value => value},
    Ui: {isMenuOpen: () => seated},
    Utility: {
      isInMenuMode: () => state.paused,
      wait: () => new Promise(resolve => jobs.push({at: frame + 1, run: resolve})),
      waitMenuMode: () => new Promise(resolve => jobs.push({at: frame + 1, run: resolve}))
    },
    getInventoryQueueFence() {
      let ready = false;
      jobs.push({at: frame + 2, run: () => { ready = true; }});
      return () => ready;
    },
    once: (_name, callback) => updates.push(callback), on() {}, printConsole() {}
  };
  const sandbox = {exports: {}, Promise, Map, Set, Date,
    require(name) {
      if (name === 'skyrimPlatform') return sp;
      if (name.endsWith('/clientListener')) return {ClientListener: class {}};
      if (name.endsWith('/worldViewMisc')) return {remoteIdToLocalId: id => id};
      if (name.endsWith('/equipment')) return {isBadMenuShown: () => state.paused};
      if (name.endsWith('/inventoryApplyCoordinator')) return {inventoryApplyFence: () => Object.assign(sp.getInventoryQueueFence(), {isCurrent:()=>true})};
      if (name.endsWith('/inventory')) return {
        getInventory(player) { player.getBaseObject(); return actual; },
        getDiff(expected, got) { return {entries: JSON.stringify(expected) === JSON.stringify(got) ? [] : expected.entries}; },
        applyInventory(player, expected) {
          player.getBaseObject(); applies.push(expected); actual = expected;
        }
      };
      if (name.endsWith('/logging')) return {logTrace() {}, logError() {}};
      if (name.endsWith('/messages')) return {MsgType: {Activate: 1}};
      return {};
    }
  };
  vm.runInNewContext(js, sandbox);
  const service = Object.create(sandbox.exports.RemoteServer.prototype);
  service.sp = sp;
  service.ownerSpawnEpoch = 0;
  service.controller = {emitter: {emit: (_name, message) => messages.push(message)}};
  function observe(promise) { Promise.resolve(promise).catch(error => errors.push(error)); }
  return {
    service, sp, state, errors, opens, applies, messages,
    pack() { observe(service.packBeforeBench()); },
    open() { service.onOpenContainerMessage({message: {target: targetId}}); },
    async advance(count = 18) {
      for (let n = 0; n < count; n++) {
        frame++; // Every previously issued wrapper now fails like the real pool.
        if (options.change && frame === 3) options.change({service, storage, state});
        if (frame === 12) seated = false;
        const due = jobs.filter(job => job.at <= frame);
        for (const job of due) { jobs.splice(jobs.indexOf(job), 1); job.run(); }
        for (const callback of updates.splice(0)) observe(callback());
        for (let flush = 0; flush < 12; flush++) await Promise.resolve();
      }
    }
  };
}

let failed = 0;
async function check(name, run) {
  try { await run(); console.log('PASS ' + name); }
  catch (error) { failed++; console.error('FAIL ' + name + ': ' + error.message); }
}
function noExpired(f) {
  assert.equal(f.errors.length, 0, f.errors.map(error => error.stack).join('\n'));
}
(async () => {
  await check('inventory is read and applied with a player reacquired after the queue drains', async () => {
    const f = fixture(); f.pack(); await f.advance(); noExpired(f);
    assert.equal(f.applies.length, 1);
  });
  await check('the furniture reference is reacquired after waiting for its pack', async () => {
    const f = fixture({current: true});
    // Isolate the caller's reference lifetime from the separate player bug.
    f.service.packBeforeBench = async () => {await f.sp.Utility.wait(0.1); return true;};
    f.open(); await f.advance(); noExpired(f);
    assert.equal(f.opens.length, 1);
    assert.equal(f.applies.length, 0);
    assert.equal(f.messages.length, 1);
    assert.equal(f.messages[0].message.data.isSecondActivation, true);
  });
  await check('furniture opens only after queued inventory has completed', async () => {
    const f = fixture(); f.open(); await f.advance(); noExpired(f);
    assert.equal(f.applies.length, 1); assert.equal(f.opens.length, 1);
    assert.ok(f.opens[0] >= 6, 'both inventory fences must run before activation');
  });
  await check('the pack received during the wait is the one applied', async () => {
    const f = fixture({change: ({storage}) => { storage.pcInv = {entries: [{baseId: 100, count: 5}]}; }});
    f.open(); await f.advance(); noExpired(f);
    assert.equal(f.applies[0].entries[0].count, 5);
  });
  await check('a menu opening during the queue wait prevents inventory mutation', async () => {
    const f = fixture({change: ({state}) => { state.paused = true; }});
    f.open(); await f.advance(); noExpired(f); assert.equal(f.applies.length, 0);
  });
  await check('a changed owner cancels the delayed activation and inventory work', async () => {
    const f = fixture({change: ({service}) => { service.ownerSpawnEpoch++; }});
    f.open(); await f.advance(); noExpired(f);
    assert.equal(f.applies.length, 0); assert.equal(f.opens.length, 0);
  });
  await check('a target unloaded during the wait is not activated', async () => {
    const f = fixture({change: ({state}) => { state.target = false; }});
    f.open(); await f.advance(); noExpired(f); assert.equal(f.opens.length, 0);
  });
  await check('containers retain their existing activation and close acknowledgement', async () => {
    const f = fixture({container: true}); f.open(); await f.advance(); noExpired(f);
    assert.equal(f.opens.length, 1); assert.equal(f.applies.length, 0);
    assert.equal(f.messages.length, 1);
  });
  if (failed) process.exitCode = 1;
})();
