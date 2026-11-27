'use strict';
// Thornswood #6492: an array index can be reused before the next world tick.
// Run the actual view array, name renderer and nickname service over native
// text stand-ins. Only actor spawning/movement is replaced by the fixture.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const ts = require('typescript');
const { EventEmitter } = require('node:events');
const root = path.join(__dirname, 'src');

function compile(source, stubs) {
  const js = ts.transpileModule(source, {
    compilerOptions: { target: ts.ScriptTarget.ES2020, module: ts.ModuleKind.CommonJS },
  }).outputText;
  const exports = {};
  new Function('require', 'exports', js)(name => stubs[name] || {}, exports);
  return exports;
}
function load(file, stubs) { return compile(fs.readFileSync(path.join(root, file), 'utf8'), stubs); }

function rig() {
  const storage = {};
  const cancelled = [];
  const texts = new Map();
  let nextId = 0;
  const emitter = new EventEmitter();
  const visibility = { distance: 10 };
  const controller = { emitter, lookupListener: () => ({ setFormViewArray() {}, setI() {} }) };
  const sp = { storage };
  const { SweetTaffyNicknamesService } = load('services/services/sweetTaffyNicknamesService.ts', {
    './clientListener': { ClientListener: class {} },
  });
  new SweetTaffyNicknamesService(sp, controller);
  const { FormView } = load('view/formView.ts', {
    '../sync/inventoryApplyCoordinator': { cancelInventoryApplication: id => cancelled.push(id) },
    skyrimPlatform: {
      storage, once() {}, Actor: { from: () => null },
      Game: { getPlayer: () => ({ getDistance: () => visibility.distance, hasLOS: () => true }) },
      Utility: { getINIInt: () => 1000 },
      NetImmerse: { getNodeWorldPositionX: () => 0, getNodeWorldPositionY: () => 0,
        getNodeWorldPositionZ: () => 0 },
      worldPointToScreenPoint: () => [[0.5, 0.5, 1]],
      createText(x, y, name, color) { const id = ++nextId; texts.set(id, { name, color }); return id; },
      destroyText(id) { assert(texts.delete(id), 'destroy a live text exactly once'); },
      setTextPos(id) { assert(texts.has(id), 'only a live text can be moved'); },
      setTextSize(id) { assert(texts.has(id), 'only a live text can be sized'); },
    },
    '../sync/animation': { setDefaultAnimsDisabled() {} },
    './modelApplyUtils': { ModelApplyUtils: new Proxy({}, { get: () => () => {} }) },
    './playerCharacterDataHolder': { PlayerCharacterDataHolder: { getCrosshairRefId: () => 0 } },
    '../services/spApiInteractor': { SpApiInteractor: { getControllerInstance: () => controller } },
  });
  // Keep the actual constructor, destruction, drawing and tag event handlers.
  // Model-to-actor spawning is outside this identity regression's scope.
  FormView.prototype.update = function (form) {
    this.refrId = 0xff000001;
    this.applyAll({ is3DLoaded: () => false, getDisplayName: () => form.appearance.name }, form);
  };
  const { FormViewArray } = load('view/formViewArray.ts', {
    './formView': { FormView },
    '../services/spApiInteractor': { SpApiInteractor: { getControllerInstance: () => controller } },
  });
  return { storage, texts, visibility, cancelled, views: new FormViewArray() };
}
const form = (refrId, name) => ({ refrId, appearance: { name } });
function tick(w, forms) {
  w.views.resize(forms.length);
  w.views.updateAll({ forms, playerCharacterFormIdx: -1 }, false, false);
}
function draw(w, forms) { tick(w, forms); tick(w, forms); }
function label(w, ref) { return w.texts.get(w.storage.idTextNickname[ref]); }
let passed = 0;
function test(name, run) { run(); ++passed; console.log('PASS ' + name); }

test('unchanged identity reuses its view and text', () => {
  const w = rig(); const a = form(0xff000101, 'A');
  draw(w, [a]);
  const view = w.views.getNthFormView(0); const id = w.storage.idTextNickname[a.refrId];
  tick(w, [a]);
  assert.equal(w.views.getNthFormView(0), view);
  assert.equal(w.storage.idTextNickname[a.refrId], id);
  assert.equal(w.texts.size, 1);
});

test('replaced slot destroys the old tag before a new owner publishes', () => {
  const w = rig(); const a = form(0xff000101, 'A'); const b = form(0xff000202, 'B');
  draw(w, [a]);
  const oldView = w.views.getNthFormView(0); const oldText = w.storage.idTextNickname[a.refrId];
  tick(w, [b]);
  assert.deepEqual(w.cancelled, [0xff000001], 'replacement cancels the old local inventory before publishing the new view');
  assert.equal(w.storage.idTextNickname[a.refrId], undefined, 'old name ownership ends with the old character');
  assert.notEqual(w.views.getNthFormView(0), oldView);
  assert.equal(w.views.getNthFormView(0).getRemoteRefrId(), b.refrId);
  assert.equal(w.texts.has(oldText), false);
  tick(w, [b]);
  assert.equal(label(w, b.refrId).name, 'B');
  assert.equal(w.texts.size, 1);
});

test('two reordered characters keep independent correctly owned name tags', () => {
  const w = rig(); const a = form(0xff000101, 'A'); const b = form(0xff000202, 'B');
  draw(w, [a, b]); draw(w, [b, a]);
  assert.equal(label(w, a.refrId).name, 'A');
  assert.equal(label(w, b.refrId).name, 'B');
  assert.equal(w.texts.size, 2);
  w.views.destroyForm(0);
  assert.equal(label(w, a.refrId).name, 'A');
  assert.equal(w.storage.idTextNickname[b.refrId], undefined);
  w.views.resize(0);
  assert.deepEqual(w.storage.idTextNickname, {});
  assert.equal(w.texts.size, 0);
});

test('identical display names remain separate people rather than deduplicated text', () => {
  const w = rig(); const a = form(0xff000101, 'Same name'); const b = form(0xff000202, 'Same name');
  draw(w, [a, b]);
  const aText = w.storage.idTextNickname[a.refrId]; const bText = w.storage.idTextNickname[b.refrId];
  assert.notEqual(aText, bText);
  assert.equal(w.texts.size, 2);
  label(w, a.refrId).color = [0, 1, 0, 1]; // Front's party colour targets this owner's table entry.
  assert.deepEqual(label(w, b.refrId).color, [1, 1, 1, 0.8]);
  tick(w, [undefined, b]);
  assert.equal(w.storage.idTextNickname[a.refrId], undefined);
  assert.equal(w.storage.idTextNickname[b.refrId], bText);
});

test('distance hiding and reappearance reuse the same character view', () => {
  const w = rig(); const a = form(0xff000101, 'A');
  draw(w, [a]); const view = w.views.getNthFormView(0);
  w.visibility.distance = 1001; tick(w, [a]);
  assert.equal(w.texts.size, 0);
  assert.equal(w.storage.idTextNickname[a.refrId], undefined);
  w.visibility.distance = 10; tick(w, [a]);
  assert.equal(w.views.getNthFormView(0), view);
  assert.equal(label(w, a.refrId).name, 'A');
  assert.equal(w.texts.size, 1);
});

test('the own-character filter destroys a previously shown view and tag', () => {
  const w = rig(); const a = form(0xff000101, 'A');
  draw(w, [a]);
  w.views.updateAll({ forms: [a], playerCharacterFormIdx: 0 }, false, false);
  assert.equal(w.views.getNthFormView(0), undefined);
  assert.equal(w.storage.idTextNickname[a.refrId], undefined);
  assert.equal(w.texts.size, 0);
});

test('real connection reset followed by create before a world tick replaces stale identity', () => {
  const w = rig(); const old = form(0xff000101, 'Old'); const fresh = form(0xff000202, 'Fresh');
  draw(w, [old]);
  const source = fs.readFileSync(path.join(root, 'services/services/remoteServer.ts'), 'utf8');
  const ast = ts.createSourceFile('remoteServer.ts', source, ts.ScriptTarget.Latest, true);
  const cls = ast.statements.find(n => ts.isClassDeclaration(n) && n.name.text === 'RemoteServer');
  const reset = cls.members.find(n => n.name?.getText(ast) === 'handleConnectionAccepted');
  assert(reset, 'use the actual connection reset method');
  // The extracted method's dependencies are injected by its lexical names.
  const js = ts.transpileModule('class Reset { ' + reset.getText(ast) + ' }', {
    compilerOptions: { target: ts.ScriptTarget.ES2020 },
  }).outputText;
  let cancelledAll = 0;
  const connectionReset = new Function('storage', 'carriedOutTeleportSeqKey', 'logTrace', 'cancelAllInventoryApplications',
    js + '\nreturn Reset.prototype.handleConnectionAccepted;')(w.storage, 'teleport-seq', () => {}, () => ++cancelledAll);
  const server = { cancelOwnerSpawn() {}, teleportsOnTheirWay: new Map(), worldModel: {
    forms: [old], playerCharacterFormIdx: -1, playerCharacterRefrId: 0 } };
  connectionReset.call(server);
  assert.equal(cancelledAll, 1, 'the actual connection reset cancels every old inventory application');
  assert.deepEqual(server.worldModel.forms, []);
  assert.equal(w.texts.size, 1, 'the old view can still exist before the next world update');
  server.worldModel.forms[0] = fresh; // The next create packet fills the reused slot.
  draw(w, server.worldModel.forms);
  assert.equal(w.storage.idTextNickname[old.refrId], undefined);
  assert.equal(label(w, fresh.refrId).name, 'Fresh');
  assert.equal(w.texts.size, 1);
});

console.log(passed + ' name-view identity tests passed');
