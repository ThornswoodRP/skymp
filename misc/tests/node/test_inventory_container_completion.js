'use strict';
// Actual activation + remote admission + inventory/coordinator + transfer
// service. This reproduces the cross-frame grant leaking into a manual diff.
const fs=require('node:fs'),path=require('node:path'),Module=require('node:module'),vm=require('node:vm');
const ts=require('../../../skymp5-client/node_modules/typescript');
const file=path.join(__dirname,'test_inventory_bench_completion.js'),source=fs.readFileSync(file,'utf8'),end=source.indexOf('(async()=>{');
if(end<0)throw Error('Actual handler fixture boundary missing');
const fixture=new Module(file,module);fixture.filename=file;fixture.paths=Module._nodeModulePaths(__dirname);fixture._compile(source.slice(0,end)+'\nmodule.exports={game,SWORD};',file);
const {game,SWORD}=fixture.exports;let pass=0,fail=0;
function yes(label,ok){console.log((ok?'PASS ':'FAIL ')+label);ok?pass++:fail++;}
function container(){
 const f=game(28);
 class Sweet{}
 f.controller.lookupListener=type=>type===f.Last?f.last:{canDropOrPutItem:()=>true};
 const dependencies=name=>{
  if(name==='skyrimPlatform')return f.sp;
  if(name.endsWith('/clientListener'))return{ClientListener:class{}};
  if(name.endsWith('/lastInvService'))return{LastInvService:f.Last};
  if(name.endsWith('/sweetTaffySweetCantDropService'))return{SweetTaffySweetCantDropService:Sweet};
  if(name.endsWith('/inventory'))return f.g.inv;
  if(name.endsWith('/remoteServer'))return f.remoteApi;
  if(name.endsWith('/worldViewMisc'))return{localIdToRemoteId:id=>id};
  if(name.endsWith('/messages'))return{MsgType:{Activate:1,PutItem:2,TakeItem:3}};
  if(name.endsWith('/logging'))return{logTrace(){},logError(){}};
  return{};
 };
 for(const [name,type]of[['activationService','ActivationService'],['containersService','ContainersService']]){
  const box={exports:{},require:dependencies};
  const input=fs.readFileSync(path.join(__dirname,'../../../skymp5-client/src/services/services',name+'.ts'),'utf8');
  vm.runInNewContext(ts.transpileModule(input,{compilerOptions:{module:ts.ModuleKind.CommonJS,target:ts.ScriptTarget.ES2018}}).outputText,box);
  new box.exports[type](f.sp,f.controller);
 }
 return f;
}
(async()=>{
 {
  const f=container(),target=[{baseId:SWORD,count:1001,name:'Grant piece'}];f.target(target);f.g.beginApply(target);await f.advance(1);
  f.handlers.activate({caster:f.g.pc,target:f.targetRef});
  const original=f.last.lastInv;f.open();await f.advance(8);
  yes('normal container admission waits while grant units are still arriving',f.opens===0&&f.g.calls.length>16&&f.g.calls.length<1001);
  await f.advance(90);
  yes('container opens after the whole current pack and refreshes the old activation baseline',f.opens===1&&f.last.lastInv!==original&&f.g.inv.getDiff({entries:target},f.last.lastInv,true).entries.length===0);
  f.sp.TESModPlatform.addItemEx(f.g.pc,f.sp.Game.getFormEx(15),1,1,null,0,false,0,'Gold',0,null,0);
  f.handlers.containerChanged({oldContainer:f.targetRef,newContainer:f.g.pc});
  const transfers=f.messages.filter(m=>m.t===3||m.t===2);
  yes('actual manual transfer sends only one gold, with no intervening grant swords',JSON.stringify(transfers.map(m=>[m.t,m.baseId,m.count]))===JSON.stringify([[3,15,1]]));
  const before=f.g.calls.length;f.g.beginApply([{baseId:SWORD,count:1021,name:'Grant piece'},{baseId:15,count:1}]);await f.advance(8);
  yes('a new desired grant does not prepare/remove/add while the container is open',f.g.calls.length===before&&f.g.nativeApply.scopes.at(-1).descriptors.length===0);
  f.stop();await f.advance(8);f.g.nativeApply.drain();
  yes('closing the container permits one fresh remaining plan',f.g.diff([{baseId:SWORD,count:1021,name:'Grant piece'},{baseId:15,count:1}]).length===0);
 }
 for(const cancel of ['owner','death','load']){
  const f=container(),target=[{baseId:SWORD,count:1001,name:'Cancelled grant'}];f.target(target);f.g.beginApply(target);await f.advance(1);f.handlers.activate({caster:f.g.pc,target:f.targetRef});
  const baseline=f.last.lastInv;f.open();await f.advance(8);
  if(cancel==='owner')f.remote.cancelOwnerSpawn();
  if(cancel==='death')f.remote.onDeathStateContainerMessage({message:{tIsDead:{idx:0,propName:'isDead',data:true}}});
  if(cancel==='load')f.g.nativeApply.emit('preLoadGame');
  await f.advance(10);
  yes(cancel+' cancellation refuses activation and preserves the prior manual baseline',f.opens===0&&f.last.lastInv===baseline);
 }
 {
  const f=container(),target=[{baseId:SWORD,count:1001}];f.target(target);f.g.beginApply(target);await f.advance(1);f.handlers.activate({caster:f.g.pc,target:f.targetRef});f.open();await f.advance(8);
  const baseline=f.last.lastInv;f.remote.onSetInventoryMessage({message:{inventory:{entries:[{baseId:SWORD,count:20}]}}});await f.advance(10);
  yes('new target cancellation does not open an old container attempt or replace its baseline',f.opens===0&&f.last.lastInv===baseline);
 }
 {
  const f=container(),target=[{baseId:SWORD,count:1001}];f.target(target);f.g.beginApply(target);await f.advance(1);f.open();await f.advance(8);
  f.targetRef.getBaseObject=()=>({getType:()=>28,getFormID:()=>0x2000});await f.advance(90);
  yes('a changed target base is refused after the inventory wait',f.opens===0);
 }
 console.log(pass+' passed, '+fail+' failed');process.exitCode=fail?1:0;
})().catch(error=>{console.error(error.stack);process.exitCode=1;});
