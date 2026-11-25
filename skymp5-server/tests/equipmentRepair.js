'use strict';
// Offline native boundary, real installed plugin records and an isolated world.
const assert = require('node:assert/strict');
const fs = require('node:fs'), path = require('node:path'), os = require('node:os');
const addon = require(path.resolve(process.argv[2]));
const installed = JSON.parse(fs.readFileSync(process.argv[3], 'utf8'));
const fixture = fs.mkdtempSync(path.join(os.tmpdir(),'tw-equipment-repair-'));
process.chdir(fixture);
const dataDir = path.join(fixture,'data');
fs.mkdirSync(path.join(dataDir,'scripts'),{recursive:true});
const mp = new addon.ScampServer(JSON.stringify({port:0,maxPlayers:2,offlineMode:true,
  dataDir,databaseDriver:'file',databaseName:path.join(fixture,'world'),
  loadOrder:installed.loadOrder,logLevel:'error'}));
mp._setSelf(mp);mp.attachSaveStorage();
assert.equal(typeof mp.compareAndSetActorInventoryAndEquipment,'function','native atomic repair method exists');
const actor = mp.createActor(0x7,[1,1,1],0,0x3c,42);
mp.set(actor,'inventory',{entries:[{baseId:0x12eb7,count:2,name:'Keepsake (25%)',health:1.4,chargePercent:0.5}]});
mp.set(actor,'equipment',{inv:{entries:[{baseId:0x12eb7,count:1,name:'Keepsake (25%)',health:1.4,chargePercent:0.5,worn:true}]}});
const inventory=mp.get(actor,'inventory'),equipment=mp.get(actor,'equipment');
const nextInventory=structuredClone(inventory),nextEquipment=structuredClone(equipment);
nextInventory.entries[0].name='Keepsake';nextEquipment.inv.entries[0].name='Keepsake';
const change={expectedInventory:inventory,replacementInventory:nextInventory,
  expectedEquipment:equipment,replacementEquipment:nextEquipment,
  expectedLifeGeneration:0,expectedRuntimeIdentity:mp.getActorRuntimeIdentity(actor)};
const commit=(patch={},profile=42)=>mp.compareAndSetActorInventoryAndEquipment(actor,profile,{...change,...patch});
for(const profile of [0,-1,1.5,NaN,'42'])assert.throws(()=>commit({},profile));
assert.equal(commit({},43),false);
assert.equal(commit({expectedLifeGeneration:1}),false);
assert.equal(commit({expectedRuntimeIdentity:'18446744073709551615'}),false);
assert.equal(commit({expectedInventory:{entries:[]}}),false);
assert.equal(commit({expectedEquipment:{...equipment,numChanges:equipment.numChanges+1}}),false);
for(const key of ['count','health','chargePercent','enchantmentId','worn','npcEquipment']) {
  const changed=structuredClone(nextInventory);changed.entries[0][key]=key==='worn'||key==='npcEquipment'?true:99;
  assert.throws(()=>commit({replacementInventory:changed}),key+' cannot change');
  assert.deepEqual(mp.get(actor,'inventory'),inventory);
  assert.deepEqual(mp.get(actor,'equipment'),equipment);
}
const differentName=structuredClone(nextEquipment);differentName.inv.entries[0].name='Wrong piece';
assert.throws(()=>commit({replacementEquipment:differentName}));
assert.throws(()=>commit({replacementEquipment:equipment}),'a worn name cannot remain stale');
const spellChange=structuredClone(nextEquipment);spellChange.leftSpell=0x12fcd;
assert.throws(()=>commit({replacementEquipment:spellChange}));
assert.throws(()=>commit({replacementInventory:{entries:[{baseId:0x12eb7,count:2,name:'x'.repeat(128*1024)}]}}));
mp.makeProperty('_skympNpcLifeGeneration',{isVisibleByOwner:false,isVisibleByNeighbors:false,updateOwner:'',updateNeighbor:''});
assert.equal(commit({replacementInventory:{toJSON(){mp.set(actor,'_skympNpcLifeGeneration',1);return nextInventory;}}}),false,
  'serialization cannot commit against a replacement life');
assert.deepEqual(mp.get(actor,'inventory'),inventory);
assert.deepEqual(mp.get(actor,'equipment'),equipment);
mp.set(actor,'_skympNpcLifeGeneration',0);
assert.equal(commit({replacementInventory:{toJSON(){
  mp.set(actor,'isDead',true);mp.set(actor,'isDead',false);return nextInventory;
}}}),false,'serialization cannot replace the runtime observed on entry');
assert.deepEqual(mp.get(actor,'inventory'),inventory);
assert.deepEqual(mp.get(actor,'equipment'),equipment);
change.expectedRuntimeIdentity=mp.getActorRuntimeIdentity(actor);
assert.equal(commit(),true);
assert.deepEqual(mp.get(actor,'inventory'),nextInventory);
assert.deepEqual(mp.get(actor,'equipment'),nextEquipment);
assert.equal(commit(),false,'replay refuses both fields');
console.log('PASS native repair: exact fences, NAME-only extras, one inventory/equipment commit');
