'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { stripTypeScriptTypes } = require('node:module');
const vm = require('node:vm');

const file = path.join(__dirname, 'src/view/modelApplyUtils.ts');
const source = fs.readFileSync(file, 'utf8');
const body = source.replace(/^import .*;\r?\n/gm, '').replace('export class ', 'class ');
const parent = {};
const FormType = { Door: 29, Container: 28 };
const { applyModelIsOpen } = vm.runInNewContext(stripTypeScriptTypes(body) + '\nModelApplyUtils;', {
  FormType,
  Game: { getForm: id => id === 0x460ca ? parent : null },
  ObjectReference: { from: value => value },
});

function fixture(state, formType = FormType.Door, baseId = 100) {
  const commands = [], activations = [];
  return {
    commands,
    activations,
    refr: {
      getBaseObject: () => ({ getType: () => formType, getFormID: () => baseId }),
      getOpenState: () => state,
      setOpen(value) { commands.push(value); },
      activate(ref, defaultOnly) { activations.push([ref, defaultOnly]); },
    },
  };
}

for (const [state, desired] of [[1, true], [2, true], [3, false], [4, false]]) {
  const { refr, commands } = fixture(state);
  for (let i = 0; i < 10; i++) applyModelIsOpen(refr, desired);
  assert.deepEqual(commands, [], `door state ${state} already matches or is moving toward ${desired}`);
}
for (const [state, desired] of [[1, false], [2, false], [3, true], [4, true], [0, true], [0, false]]) {
  const { refr, commands } = fixture(state);
  applyModelIsOpen(refr, desired);
  assert.deepEqual(commands, [desired], `door state ${state} still needs ${desired}`);
}
const container = fixture(1, FormType.Container);
applyModelIsOpen(container.refr, true);
assert.deepEqual(container.commands, [true]);
const secretDoor = fixture(3, FormType.Door, 0x6f703);
applyModelIsOpen(secretDoor.refr, true);
assert.deepEqual(secretDoor.commands, [true]);
assert.deepEqual(secretDoor.activations, [[parent, false]]);
console.log('PASS door sync preserves active transitions, reversals, containers and scripted doors');
