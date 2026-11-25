'use strict';
// Execute the real receiver and FormView equipment branch. Name-only packets
// refresh the cached record without running the destructive re-equip path.
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),assert=require('node:assert/strict');
const ts=require('../../../skymp5-client/node_modules/typescript');
const root=path.join(__dirname,'../../../skymp5-client/src');
const source=fs.readFileSync(path.join(root,'services/services/remoteServer.ts'),'utf8');
const js=ts.transpileModule(source,{compilerOptions:{module:ts.ModuleKind.CommonJS,target:ts.ScriptTarget.ES2018}}).outputText;
const storage={};
const sandbox={exports:{},require(name){
  if(name==='skyrimPlatform')return{on(){},storage,Game:{}};
  if(name.endsWith('/clientListener'))return{ClientListener:class{}};
  if(name.endsWith('/logging'))return{logError(){throw Error('unexpected missing model');}};
  return{};
}};
vm.runInNewContext(js,sandbox);
const old={inv:{entries:[{baseId:0x12eb7,count:1,name:'Keepsake (25%)',worn:true}]},numChanges:8};
const repaired=structuredClone(old);repaired.inv.entries[0].name='Keepsake';
const service=Object.create(sandbox.exports.RemoteServer.prototype);
storage.worldModel={forms:[{equipment:old}]};service.getIdManager=()=>({getId:()=>0});
service.onUpdateEquipmentMessage({message:{idx:123,data:repaired}});
assert.equal(service.worldModel.forms[0].equipment,repaired,'the real receiver updates names even at the same counter');
const viewSource=fs.readFileSync(path.join(root,'view/formView.ts'),'utf8');
const start=viewSource.indexOf('    if (model.equipment) {');
const end=viewSource.indexOf('    if (FormView.isDisplayingNicknames',start);
assert(start>=0&&end>start);
const apply=new Function('model','refr','Actor','isBadMenuShown','Date','applyEquipment',viewSource.slice(start,end));
const view={eqState:{lastNumChanges:8,lastEqMoment:0},spawnMoment:1};
let replacements=0;
const run=model=>apply.call(view,model,{}, {from:r=>r},()=>false,{now:()=>10000},()=>{replacements++;return true;});
run(service.worldModel.forms[0]);
assert.equal(replacements,0,'same-counter names do not invoke removeAllItems/unequipAll equipment application');
const next=structuredClone(repaired);next.numChanges=9;
service.onUpdateEquipmentMessage({message:{idx:123,data:next}});run(service.worldModel.forms[0]);
assert.equal(replacements,1,'a real equipment change still enters the normal branch');
console.log('PASS real receiver/model: names refresh at the same counter without re-equipping');
