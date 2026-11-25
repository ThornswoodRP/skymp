'use strict';
const assert = require('node:assert/strict');

try {
  const stool = 0x74ec6;
  const marker = '12345678-1234-1234-1234-123456789abc';
  const property = 'thornswoodFurniturePlacement';
  const options = { isVisibleByOwner: false, isVisibleByNeighbors: false,
    updateOwner: '', updateNeighbor: '' };
  const before = mp.getLoadedFormCount();
  assert.throws(() => mp.placeDisabledFurniture(stool, marker), /hidden/);
  assert.equal(mp.getLoadedFormCount(), before);
  mp.makeProperty(property, { ...options, isVisibleByOwner: true });
  assert.throws(() => mp.placeDisabledFurniture(stool, marker), /hidden/);
  assert.equal(mp.getLoadedFormCount(), before);
  mp.clear();
  const badMarker = '123456781234-1234-1234-123456789-abc';
  mp.makeProperty(property, options);
  assert.throws(() => mp.placeDisabledFurniture(stool, 'invalid'), /marker/i);
  assert.throws(() => mp.placeDisabledFurniture(stool, badMarker), /marker/i);
  assert.throws(() => mp.placeDisabledFurniture(0xf, marker), /base/);
  assert.equal(mp.getLoadedFormCount(), before);

  const staged = mp.placeDisabledFurniture(stool, marker);
  assert.equal(mp.get(staged, 'isDisabled'), true);
  assert.equal(mp.get(staged, property), marker);
  assert.equal(mp.placeDisabledFurniture(stool, marker), staged);
  assert.throws(() => mp.placeDisabledFurniture(0x2ec1c, marker), /unexpected/);
  assert.equal(mp.getLoadedFormCount(), before + 1);
  assert.equal(mp.getNeighborsByPosition('3c:Skyrim.esm', [0,0,0]).includes(staged), false);
  mp.set(staged, 'isDisabled', false);
  assert.ok(mp.getNeighborsByPosition('3c:Skyrim.esm', [0,0,0]).includes(staged));

  const direct = mp.place(stool);
  assert.equal(mp.get(direct, 'isDisabled'), false);
  assert.ok(mp.get(direct, property) == null);
  console.log('Test passed!');
  process.exit(0);
} catch (error) {
  console.error(error);
  console.log('Test failed!');
  process.exit(1);
}
