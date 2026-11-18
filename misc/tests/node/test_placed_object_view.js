'use strict';
// Apply a live transform to two already-spawned client views of one board.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const ts = require('typescript');
const root = path.resolve(__dirname, '../../../skymp5-client/src');
function method(file, name, env) {
  const source = fs.readFileSync(path.join(root, file), 'utf8');
  const parsed = ts.createSourceFile(file, source, ts.ScriptTarget.Latest, true);
  const cls = parsed.statements.find(ts.isClassDeclaration);
  const member = cls.members.find((m) => m.name?.getText(parsed) === name);
  assert(member, name);
  const js = ts.transpileModule(`class Subject { ${member.getText(parsed)} }`, {
    compilerOptions: { target: ts.ScriptTarget.ES2020 },
  }).outputText;
  return new Function(...Object.keys(env), js + `\nreturn Subject.prototype.${name};`)(...Object.values(env));
}
const receive = method('services/services/remoteServer.ts', 'onUpdatePropertyMessage', {});
const moves = [];
const apply = method('view/formView.ts', 'applyAll', {
  FormView: {}, Actor: { from: () => null }, storage: {},
  PlayerCharacterDataHolder: { getCrosshairRefId: () => 0 },
  ModelApplyUtils: {}, setDefaultAnimsDisabled() {}, applyMovement() {},
  Game: { getFormEx: (id) => id }, Cell: { from: () => null }, WorldSpace: { from: (id) => id },
  TESModPlatform: { moveRefrToPosition: (...args) => moves.push(args) },
});
const idx = 12;
const board = 0xff000100;
const clients = [0xff005000, 0xff006000].map((id) => {
  const form = { idx, refrId: board, movement: { pos: [0, 0, 0], rot: [0, 0, 0], worldOrCell: 60 } };
  return {
    form, ref: { id, is3DLoaded: () => false },
    view: { removeNickname() {}, movState: {}, isSetNodeScaleApplied: true, isSetNodeTextureSetApplied: true },
    server: { worldModel: { forms: [form] }, getIdManager: () => ({ getId: () => 0 }),
      skipFormViewCreation: () => false, extractUpdatePropertyMessageData: (m) => JSON.parse(m.dataDump) },
  };
});
for (const transform of [
  { pos: [0, 0, 0], rot: [0, 0, 90], worldOrCell: 60 },
  { pos: [11, 22, 33], rot: [0, 0, 90], worldOrCell: 60 },
]) {
  moves.length = 0;
  for (const { form, ref, view, server } of clients) {
    receive.call(server, { message: { idx, refrId: board, propName: 'placedTransform', dataDump: JSON.stringify(transform) } });
    view.lastHarvestedApply = view.lastOpenApply = Date.now();
    apply.call(view, ref, form);
    apply.call(view, ref, form);
  }
  assert.deepEqual(moves, clients.map(({ ref }) => [ref, null, 60, ...transform.pos, 0, 0, Math.PI / 2]));
  assert.deepEqual(clients.map(({ form }) => form.movement.pos), [transform.pos, transform.pos]);
}
console.log('PASS: both existing client views move and rotate once per update and retain the latest spawn transform');
