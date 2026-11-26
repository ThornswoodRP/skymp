'use strict';
// The actual squash and comparator, with an unchanged old-algorithm oracle.
// No Skyrim/native execution and no elapsed-time assertion.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const ts = require('../../../skymp5-client/node_modules/typescript');
const file = process.env.INVENTORY || path.join(__dirname, '../../../skymp5-client/src/sync/inventory.ts');
const source = fs.readFileSync(file, 'utf8');
const js = ts.transpileModule(source, {
  compilerOptions: { module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2018 },
}).outputText;
const SWORD = 0x12eb7, BOOTS = 0x12e4b;
const sp = { storage: {}, Game: { getFormEx: id =>
  id === SWORD || id === BOOTS ? { getName: () => id === SWORD ? 'Iron Sword' : 'Boots' } : null } };
const scope = { exports: {}, require: name => name === 'skyrimPlatform' ? sp : {} };
scope.module = { exports: scope.exports };
vm.runInNewContext(`globalThis.splitCalls = 0;
  const nativeSplit = String.prototype.split;
  String.prototype.split = function (...args) { ++globalThis.splitCalls; return nativeSplit.apply(this, args); };`, scope);
vm.runInNewContext(js + '\nexports.squashForTest = squash;', scope, { filename: file });
const api = scope.exports;
const legacy = (inventory, ignoreWorn = false) => {
  const entries = [];
  inventory.entries.forEach(entry => {
    const same = entries.find(other => api.inventoryEntriesEqual(other, entry, ignoreWorn));
    if (same) same.count += entry.count;
    else entries.push(JSON.parse(JSON.stringify(entry)));
  });
  return { entries: entries.filter(entry => entry.count !== 0) };
};
let passed = 0;
function same(label, makeEntries, ignoreWorn = false) {
  const actualInput = { entries: makeEntries() }, expectedInput = { entries: makeEntries() };
  const before = JSON.stringify(actualInput);
  let actual, expected, actualError, expectedError;
  try { actual = api.squashForTest(actualInput, ignoreWorn); } catch (e) { actualError = e; }
  try { expected = legacy(expectedInput, ignoreWorn); } catch (e) { expectedError = e; }
  assert.equal(actualError?.name, expectedError?.name, label + ' exception type');
  if (!actualError) assert.equal(JSON.stringify(actual), JSON.stringify(expected), label + ' values/order/representative');
  assert.equal(JSON.stringify(actualInput), before, label + ' input unchanged');
  ++passed;
}
same('cropped names keep their common representative', () => [
  { baseId: SWORD, count: 1, name: 'x'.repeat(128) + 'A', marker: 'first' },
  { baseId: SWORD, count: 2, name: 'x'.repeat(128) + 'B', marker: 'second' },
]);
same('earlier broad name representative wins', () => [
  { baseId: SWORD, count: 1, marker: 'broad' },
  { baseId: SWORD, count: 2, name: 'Iron Sword', marker: 'exact' },
  { baseId: SWORD, count: -1, name: 'Iron Sword' },
]);
same('zero intermediate count retains representative order', () => [
  { baseId: SWORD, count: 1, name: 'A', marker: 'first' },
  { baseId: BOOTS, count: 1, name: 'B' },
  { baseId: SWORD, count: -1, name: 'A' },
  { baseId: SWORD, count: 3, name: 'A', marker: 'later' },
]);
same('JSON-cloned health changes classification', () => [
  { baseId: SWORD, count: 1, name: 'A', health: NaN, marker: 'nan-first' },
  { baseId: SWORD, count: 2, name: 'A' },
]);
same('JSON-cloned name becomes a broad representative', () => [
  { baseId: SWORD, count: 1, name: NaN, marker: 'nan-first' },
  { baseId: SWORD, count: 2, name: 'Iron Sword' },
]);
same('tempered suffix and durability rules stay on the comparator', () => [
  { baseId: SWORD, count: 1, name: 'A', health: 1.1 },
  { baseId: SWORD, count: 1, name: 'A (Fine)', health: 1.100000023841858 },
  { baseId: SWORD, count: 1, name: 'A (85%)', health: 1.1 },
  { baseId: SWORD, count: 1, name: 'A (84%)', health: 1.1 },
]);
for (const ignoreWorn of [false, true]) same('worn policy ' + ignoreWorn, () => [
  { baseId: SWORD, count: 1, name: 'A', worn: true },
  { baseId: SWORD, count: 2, name: 'A', wornLeft: true },
  { baseId: SWORD, count: 3, name: 'A' },
], ignoreWorn);
for (const field of ['baseId', 'name', 'health', 'made', 'chargePercent']) {
  const counts = [];
  function run(implementation) {
    let reads = 0;
    const entry = { baseId: SWORD, count: 2, name: 'A' };
    const values = { baseId: SWORD, name: 'A', health: 1, made: undefined, chargePercent: 0 };
    Object.defineProperty(entry, field, { enumerable: true, get() { ++reads; return values[field]; } });
    const result = implementation({ entries: [{ baseId: BOOTS, count: 1, name: 'B' },
      { baseId: SWORD, count: 1, name: 'A' }, entry] });
    counts.push(reads);
    return JSON.stringify(result);
  }
  assert.equal(run(api.squashForTest), run(legacy), field + ' accessor output');
  assert.equal(counts[0], counts[1], field + ' accessor read behavior');
  ++passed;
}
let random = 0x6721;
const next = length => { random = (Math.imul(random, 1664525) + 1013904223) >>> 0; return random % length; };
const bases = [SWORD, BOOTS, 0, -1, NaN, Infinity, undefined, null, '123'];
const names = ['A', 'B', 'Iron Sword', '', undefined, null, 0, false, 'A (Fine)', 'A (85%)'];
const health = [undefined, null, 1, 1.04, 1.1, 1.100000023841858, 0, NaN, Infinity, '1.1'];
for (let test = 0; test < 100; ++test) {
  const rows = Array.from({ length: 50 }, () => ({ baseId: bases[next(bases.length)],
    count: next(5) - 2, name: names[next(names.length)], health: health[next(health.length)],
    worn: [undefined, false, true, 0, 'false'][next(5)],
    chargePercent: [undefined, null, 0, NaN, '0'][next(5)] }));
  same('legacy mixture ' + test, () => rows.map(row => ({ ...row })), !!(test % 2));
}
for (const size of [100, 200, 400]) {
  const entries = Array.from({ length: size }, (_, i) => ({
    baseId: SWORD, count: 1, name: (String(i) + ':').padEnd(128, 'x') }));
  scope.splitCalls = 0;
  const result = api.squashForTest({ entries });
  assert.equal(result.entries.length, size);
  assert(scope.splitCalls <= size * 4,
    `${size} distinct ordinary names should do linear crop work; got ${scope.splitCalls}`);
  ++passed;
}
console.log(`${passed} snapshot index controls passed`);
