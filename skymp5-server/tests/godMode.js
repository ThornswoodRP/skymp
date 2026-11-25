'use strict';
// Offline native-boundary check: compiled addon, installed plugin order, and
// an isolated fixture directory. Never load a live saved world or users.
const assert = require('node:assert/strict');
const fs = require('node:fs'), path = require('node:path');
const addon = require(path.resolve(process.argv[2]));
const installed = JSON.parse(fs.readFileSync(process.argv[3], 'utf8'));
assert(Array.isArray(installed.loadOrder) && installed.loadOrder.length);
const fixture = fs.mkdtempSync(path.resolve(process.argv[4], 'godmode-'));
const dataDir = path.join(fixture, 'data');
fs.mkdirSync(path.join(dataDir, 'scripts'), { recursive: true });
process.chdir(fixture);
const mp = new addon.ScampServer(JSON.stringify({
  port: 0, maxPlayers: 1, offlineMode: true, logLevel: 'error', dataDir,
  npcEnabled: true, npcAllowEssential: true, npcAllowCrimeFaction: true,
  loadOrder: installed.loadOrder
}));
mp._setSelf(mp);
const actor = mp.createActor(0, [0, 0, 0], 0, 0x3c, 42);
assert.equal(mp.get(actor, 'isGodMode'), false);
for (const malformed of [1, 0, 'true', null, {}, []]) {
  assert.throws(() => mp.set(actor, 'isGodMode', malformed), /boolean/i);
  assert.equal(mp.get(actor, 'isGodMode'), false);
}
mp.set(actor, 'isGodMode', true);
assert.equal(mp.get(actor, 'isGodMode'), true);
mp.set(actor, 'isDead', true);
assert.equal(mp.get(actor, 'isDead'), false);
mp.set(actor, 'percentages', { health: 0, magicka: 1, stamina: 1 });
assert.equal(mp.get(actor, 'isDead'), false);
assert(mp.get(actor, 'percentages').health > 0);
mp.set(actor, 'isGodMode', false);
mp.set(actor, 'isDead', true);
assert.equal(mp.get(actor, 'isDead'), true);
assert.throws(() => mp.set(actor, 'isGodMode', true), /dead|unavailable/i);
assert.equal(mp.get(actor, 'isGodMode'), false);
assert.equal(mp.get(actor, 'isDead'), true);
assert.equal(typeof mp.get(0x13487, 'baseDesc'), 'string'); // Load Belethor's authored reference.
const npc = mp.getNpcAIState(0x13487);
for (const key of ['isEssential', 'isProtected', 'isUnique']) {
  assert.equal(typeof npc[key], 'boolean', key + ' is native, not an absent custom value');
}
assert.equal(npc.isUnique, true);
console.log('PASS strict native god-mode binding, damage/death refusal and NPC safety flags');
