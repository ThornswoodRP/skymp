'use strict';
// Offline compiled-addon boundary check; synthetic plugins and no actors,
// connections, saved world, or installed-game assumptions.
// node localizedNames.js <scam_native.node> <existing fixture parent>
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const addon=require(path.resolve(process.argv[2]));
const fixture=fs.mkdtempSync(path.resolve(process.argv[3],'localized-names-'));
const dataDir=path.join(fixture,'data');fs.mkdirSync(path.join(dataDir,'scripts'),{recursive:true});
function u16(n){const b=Buffer.alloc(2);b.writeUInt16LE(n);return b;}
function u32(n){const b=Buffer.alloc(4);b.writeUInt32LE(n);return b;}
function z(s){return Buffer.from(s+'\0');}
function field(type,b){return Buffer.concat([Buffer.from(type),u16(b.length),b]);}
function record(type,flags,id,fields){return Buffer.concat([Buffer.from(type),u32(fields.length),u32(flags),u32(id),u32(0),u16(44),u16(0),fields]);}
function group(records){const bytes=Buffer.concat(records);return Buffer.concat([Buffer.from('GRUP'),u32(bytes.length+24),Buffer.from('MISC'),u32(0),u32(0),u32(0),bytes]);}
function misc(id,flags,full){return record('MISC',flags,id,Buffer.concat([field('EDID',z('WorkshopToken')),field('FULL',full)]));}
function plugin(name,flags,masters,records){
  const hedr=Buffer.alloc(12);hedr.writeFloatLE(1.7);hedr.writeUInt32LE(records.length,4);hedr.writeUInt32LE(0x900,8);
  const fields=[field('HEDR',hedr)];for(const master of masters)fields.push(field('MAST',z(master)),field('DATA',Buffer.alloc(8)));
  const file=path.join(dataDir,name);fs.writeFileSync(file,Buffer.concat([record('TES4',flags,0,Buffer.concat(fields)),group(records)]));return file;
}
const base=plugin('Base.esm',0x80,[],[misc(0x800,0,u32(0x4a2e)),misc(0x801,0,u32(0x4a2e))]);
const plain=plugin('Plain.esp',0,['Base.esm'],[misc(0x800,0x80,z('Ink')),misc(0x01000800,0x80,z('Ruby')),misc(0x01000801,0x80,z('Ruby'))]);
const localized=plugin('Localized.esp',0x80,['Base.esm','Plain.esp'],[misc(0x01000800,0,u32(0x4a2e))]);
process.chdir(fixture);
const mp=new addon.ScampServer(JSON.stringify({port:0,maxPlayers:1,offlineMode:true,logLevel:'error',dataDir,npcEnabled:false,loadOrder:[base,plain,localized]}));
mp._setSelf(mp);
for(const [id,fileIndex,isLocalized,bytes]of [[0x801,0,true,[46,74,0,0]],[0x800,1,false,[73,110,107,0]],[0x01000800,2,true,[46,74,0,0]],[0x01000801,1,false,[82,117,98,121,0]]]){
  const result=mp.lookupEspmRecordById(id);
  assert.equal(result.fileIndex,fileIndex);assert.equal(result.isLocalized,isLocalized);
  assert.deepEqual(Array.from(result.record.fields.find(f=>f.type==='FULL').data),bytes);
  assert.equal(!!(result.record.flags&0x80),!isLocalized,'record flags deliberately disagree');
}
assert.equal(mp.lookupEspmRecordById(0x999999).isLocalized,undefined);
console.log('PASS native localization follows actual winning TES4, preserving both raw IDs and short text');
