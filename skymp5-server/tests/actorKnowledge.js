'use strict';
// Offline native-boundary regression, with installed plugins and an isolated world.
const assert = require('node:assert/strict');
const fs = require('node:fs'), path = require('node:path'), os = require('node:os');
const addon = require(path.resolve(process.argv[2]));
const installed = JSON.parse(fs.readFileSync(process.argv[3], 'utf8'));
assert(Array.isArray(installed.loadOrder) && installed.loadOrder.length);
const fixture = fs.mkdtempSync(path.join(os.tmpdir(), 'tw-knowledge-'));
process.chdir(fixture);
const dataDir = path.join(fixture, 'data');
fs.mkdirSync(path.join(dataDir, 'scripts'), {recursive: true});
const mp = new addon.ScampServer(JSON.stringify({port: 0, maxPlayers: 2,
  offlineMode: true, dataDir, databaseDriver: 'file',
  databaseName: path.join(fixture, 'world'), loadOrder: installed.loadOrder,
  logLevel: 'error'}));
mp._setSelf(mp);
mp.attachSaveStorage();
const actor = mp.createActor(0x7, [1, 1, 1], 0, 0x3c, 42);
assert.equal(typeof mp.getActorLearnedSpells, 'function');
const register = (name, owner = true, neighbors = false) => mp.makeProperty(name, {
  isVisibleByOwner: owner, isVisibleByNeighbors: neighbors, updateOwner: '', updateNeighbor: ''});
register('thornswoodCharacter');
const initial = {v: 1, prof: 'mage', spellHistory: {spells: ['Flames']}};
mp.set(actor, 'thornswoodCharacter', initial);
const inventory = mp.get(actor, 'inventory');
const self = {type: 'form', desc: mp.getDescFromId(actor)};
const spells = () => {
  const count = mp.callPapyrusFunction('method', 'Actor', 'GetSpellCount', self, []);
  assert.equal(typeof count, 'number');
  return Array.from({length: count}, (_, index) => {
    const spell = mp.callPapyrusFunction('method', 'Actor', 'GetNthSpell', self, [index]);
    assert.equal(typeof spell.desc, 'string');
    return mp.getIdFromDesc(spell.desc);
  }).sort((a, b) => a - b);
};
assert.deepEqual(spells(), []);
assert.deepEqual(mp.getActorLearnedSpells(actor), []);
const ability = 0x1031d3; // PCHealRateCombat is inherited by the Player base.
const property = {name: 'thornswoodCharacter', expected: initial,
  replacement: {...initial, prof: 'hunter'}, expectedLifeGeneration: 0,
  expectedRuntimeIdentity: mp.getActorRuntimeIdentity(actor),
  expectedSpells: [], replacementSpells: [0x12fcd, 0x12fcc, ability]};
const commit = (change = {}, profile = 42) =>
  mp.compareAndSetActorPropertyAndSpells(actor, profile, {...property, ...change});
for (const profile of [0, -1, 1.5, NaN, '42']) assert.throws(() => commit({}, profile));
assert.equal(commit({}, 43), false);
for (const spells of [null, {}, [NaN], [1.5], [-1], ['78541'], [0xff000001], [15], [0x12fcd, 0x12fcd]]) {
  assert.throws(() => commit({replacementSpells: spells}));
  assert.deepEqual(mp.get(actor, 'thornswoodCharacter'), initial);
}
assert.equal(commit({expected: {}}), false);
assert.equal(commit({expectedSpells: [0x12fcd]}), false);
assert.equal(commit({expectedLifeGeneration: 1}), false);
assert.equal(commit({expectedRuntimeIdentity: '18446744073709551615'}), false);
for (const name of ['missing', '_skympAuthoritativeSpells', 'inventory']) {
  assert.throws(() => commit({name}));
}
register('neighborKnowledge', true, true);
assert.throws(() => commit({name: 'neighborKnowledge'}));
assert.throws(() => commit({replacement: 'x'.repeat(1024 * 1024)}));
assert.equal(commit(), true, 'owner-visible property is accepted');
assert.deepEqual(mp.get(actor, 'thornswoodCharacter'), property.replacement);
assert.deepEqual(mp.get(actor, 'inventory'), inventory);
assert.deepEqual(spells(), [0x12fcc, 0x12fcd]);
assert.deepEqual(mp.getActorLearnedSpells(actor), [0x12fcc, 0x12fcd, ability]);
assert.equal(mp.get(actor, '_skympAuthoritativeSpells'), true);
assert.equal(commit(), false, 'replay refuses both state and spell changes');
assert.equal(commit({expected: property.replacement,
  expectedSpells: spells(), replacementSpells: [0x12fcc, ability]}), false,
  'Papyrus-filtered lists are not full-set fences');
assert.equal(commit({expected: property.replacement,
  expectedSpells: [ability, 0x12fcc, 0x12fcd], replacementSpells: [0x12fcc, ability]}), true,
  'expected spells compare as a set, preserving the unrelated remaining spell');
assert.deepEqual(spells(), [0x12fcc]);
assert.equal(commit({expected: property.replacement,
  expectedSpells: [0x12fcc, ability], replacementSpells: [ability]}), true);
assert.deepEqual(spells(), []);
assert.deepEqual(mp.getActorLearnedSpells(actor), [ability]);
console.log('PASS native character knowledge CAS validation, owner-visible state and full-set fences');
