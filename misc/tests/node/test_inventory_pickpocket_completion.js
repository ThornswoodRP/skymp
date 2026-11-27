'use strict';
// Real remote handler, ModelApplyUtils, inventory/coordinator and transfer service.
// Separate native inventories are essential: a target job cannot fill the owner.
const fs=require('node:fs'),path=require('node:path'),Module=require('node:module'),vm=require('node:vm');
const ts=require('../../../skymp5-client/node_modules/typescript');
const file=path.join(__dirname,'test_inventory_bench_completion.js'),source=fs.readFileSync(file,'utf8'),end=source.indexOf('(async()=>{');
if(end<0)throw Error('Actual handler fixture boundary missing');
const fixture=new Module(file,module);fixture.filename=file;fixture.paths=Module._nodeModulePaths(__dirname);
fixture._compile(source.slice(0,end)+'\nmodule.exports={game,SWORD};',file);
const {game,SWORD}=fixture.exports;let pass=0,fail=0;
function yes(label,ok){console.log((ok?'PASS ':'FAIL ')+label);ok?pass++:fail++;}
function pickpocket(){
 const f=game(43),sp=f.sp,g=f.g,target=f.targetRef;
 sp.FormType.NPC=43;target.isDead=()=>false;target.isDisabled=()=>false;
 g.pc.isDead=()=>false;g.pc.isDisabled=()=>false;
 sp.storage.basesReset.add(0x1000);
 const fields=['lists','base','loose'];
 const bags=new Map([[0x14,Object.fromEntries(fields.map(k=>[k,g[k]]))],
   [target.getFormID(),{lists:[],base:{},loose:{}}]]);
 function inBag(id,fn){
  const bag=bags.get(id);if(!bag)throw Error('Unknown inventory owner '+id);
  const old=Object.fromEntries(fields.map(k=>[k,g[k]]));
  for(const k of fields)g[k]=bag[k];
  try{return fn();}finally{for(const k of fields){bag[k]=g[k];g[k]=old[k];}}
 }
 const extra=sp.getExtraContainerChanges,remove=sp.removeInventoryItems,add=sp.TESModPlatform.addItemEx;
 sp.getExtraContainerChanges=id=>inBag(id,()=>extra(id));
 sp.removeInventoryItems=(id,...args)=>inBag(id,()=>remove(id,...args));
 const adds=[];
 sp.TESModPlatform.addItemEx=(ref,...args)=>inBag(ref.getFormID(),()=>{adds.push(ref.getFormID());return add(ref,...args);});
 const applyFile=path.join(__dirname,'../../../skymp5-client/src/view/modelApplyUtils.ts');
 const box={exports:{},require(name){if(name==='skyrimPlatform')return sp;if(name.endsWith('/inventory'))return g.inv;return {};}};
 vm.runInNewContext(ts.transpileModule(fs.readFileSync(applyFile,'utf8'),{compilerOptions:{module:ts.ModuleKind.CommonJS,target:ts.ScriptTarget.ES2018}}).outputText,box);
 f.modelApply=box.exports.ModelApplyUtils;
 const remoteFile=path.join(__dirname,'../../../skymp5-client/src/services/services/remoteServer.ts');
 const remoteBox={exports:{},Date:{now:()=>100},Promise,require(name){
  if(name==='skyrimPlatform')return sp;
  if(name.endsWith('/inventory'))return g.inv;
  if(name.endsWith('/inventoryApplyCoordinator'))return g.nativeApply.coordinator;
  if(name.endsWith('/modelApplyUtils'))return{ModelApplyUtils:f.modelApply};
  if(name.endsWith('/clientListener'))return{ClientListener:class{}};
  if(name.endsWith('/equipment'))return{isBadMenuShown:()=>sp.Ui.isMenuOpen('ContainerMenu')};
  if(name.endsWith('/lastInvService'))return{LastInvService:f.Last};
  if(name.endsWith('/worldViewMisc'))return{remoteIdToLocalId:id=>f.mapping===undefined?id:f.mapping,getViewFromStorage:()=>null};
  if(name.endsWith('/logging'))return{logError(){},logTrace(){}};
  return {};
 }};
 vm.runInNewContext(ts.transpileModule(fs.readFileSync(remoteFile,'utf8'),{compilerOptions:{module:ts.ModuleKind.CommonJS,target:ts.ScriptTarget.ES2018}}).outputText,remoteBox);
 const remote=Object.create(remoteBox.exports.RemoteServer.prototype);
 Object.assign(remote,{sp,controller:f.controller,ownerSpawnEpoch:0,raceMenuRequest:0,numSetInventory:0,teleportsOnTheirWay:new Map()});
 f.remote=remote;
 const model={refrId:target.getFormID(),inventory:{entries:[]}};
 sp.storage.worldModel.forms.push(model);
 let opened=false;
 sp.Ui.isMenuOpen=name=>name==='ContainerMenu'&&opened;
 const activate=target.activate;target.activate=(...args)=>{opened=true;return activate(...args);};
 f.open=()=>remote.onOpenContainerMessage({message:{target:target.getFormID()}});
 f.target=entries=>{sp.storage.pcInv={entries};remote.ownerInventory=sp.storage.pcInv;};
 class Sweet{}
 f.controller.lookupListener=type=>type===f.Last?f.last:{canDropOrPutItem:()=>true};
 const transferBox={exports:{},require(name){
  if(name==='skyrimPlatform')return sp;
  if(name.endsWith('/clientListener'))return{ClientListener:class{}};
  if(name.endsWith('/lastInvService'))return{LastInvService:f.Last};
  if(name.endsWith('/sweetTaffySweetCantDropService'))return{SweetTaffySweetCantDropService:Sweet};
  if(name.endsWith('/inventory'))return g.inv;
  if(name.endsWith('/remoteServer'))return remoteBox.exports;
  if(name.endsWith('/worldViewMisc'))return{localIdToRemoteId:id=>id};
  if(name.endsWith('/messages'))return{MsgType:{Activate:1,PutItem:2,TakeItem:3}};
  return {};
 }};
 const transferFile=path.join(__dirname,'../../../skymp5-client/src/services/services/containersService.ts');
 vm.runInNewContext(ts.transpileModule(fs.readFileSync(transferFile,'utf8'),{compilerOptions:{module:ts.ModuleKind.CommonJS,target:ts.ScriptTarget.ES2018}}).outputText,transferBox);
 new transferBox.exports.ContainersService(sp,f.controller);
 return Object.assign(f,{model,adds,owner:()=>g.inv.getInventory(g.pc),mark:()=>g.inv.getInventory(target),close(){opened=false;f.stop();}});
}
(async()=>{
 {
  const f=pickpocket(),owner=[{baseId:SWORD,count:1001,name:'Owner grant'}],mark=[{baseId:SWORD,count:1001,name:'Target pack',health:1.4}];
  f.target(owner);f.model.inventory={entries:mark};f.g.beginApply(owner);f.open();await f.advance(8);
  yes('a partial owner grant cannot open a pickpocket window',f.opens===0);
  await f.advance(90);
  yes('a partial target pack still cannot open the window after the owner drains',f.opens===0&&f.adds.includes(0x99));
  await f.advance(100);
  yes('both real inventory jobs drain before exactly one native activation',f.opens===1&&f.g.inv.getDiff({entries:owner},f.owner(),true).entries.length===0&&f.g.inv.getDiff({entries:mark},f.mark(),true).entries.length===0);
  yes('the owner manual-transfer baseline is captured after the full grant',!!f.last.lastInv&&f.g.inv.getDiff({entries:owner},f.last.lastInv,true).entries.length===0);
  f.sp.TESModPlatform.addItemEx(f.g.pc,f.sp.Game.getFormEx(15),1,1,null,0,false,0,'',0,null,0);
  f.handlers.containerChanged({oldContainer:f.targetRef,newContainer:f.g.pc});
  yes('a genuine take sends only its gold and never unfinished owner or target swords',JSON.stringify(f.messages.filter(m=>m.t===2||m.t===3).map(m=>[m.t,m.baseId,m.count,m.target]))===JSON.stringify([[3,15,1,0x99]]));
  f.close();
 }
 for(const change of ['owner','load','target','in-place-target','destroyed-model','base','mapping','dead','owner-dead']){
  const f=pickpocket();f.target([]);f.model.inventory={entries:[{baseId:SWORD,count:1001,name:'Waiting target'}]};f.open();await f.advance(10);
  const baseline=f.last.lastInv;
  if(change==='owner')f.remote.cancelOwnerSpawn();
  if(change==='load')f.g.nativeApply.emit('preLoadGame');
  if(change==='target')f.model.inventory={entries:[{baseId:SWORD,count:3}]};
  if(change==='in-place-target')f.model.inventory.entries[0].count=3;
  if(change==='destroyed-model')f.sp.storage.worldModel.forms[1]=undefined;
  if(change==='base')f.targetRef.getBaseObject=()=>({getType:()=>43,getFormID:()=>0x2000});
  if(change==='mapping')f.mapping=0x88;
  if(change==='dead')f.targetRef.isDead=()=>true;
  if(change==='owner-dead')f.g.pc.isDead=()=>true;
  await f.advance(2);
  if(change!=='in-place-target')yes(change+' invalidation cancels the old target job before its remaining units run',
   f.g.nativeApply.scopes.some(s=>s.id===0x99&&s.state()==='cancelled'));
  await f.advance(110);
  yes(change+' invalidation refuses delayed activation and retains the previous baseline',f.opens===0&&f.last.lastInv===baseline);
  f.close();
 }
 {
  const f=pickpocket(),late=[{baseId:SWORD,count:37,name:'Late owner grant'}];
  f.target([]);f.model.inventory={entries:[{baseId:SWORD,count:1001,name:'Target pack'}]};f.open();await f.advance(10);
  f.target(late);f.g.beginApply(late);await f.advance(110);
  yes('a grant arriving during the target wait also finishes before activation and baseline capture',
   f.opens===1&&f.g.inv.getDiff({entries:late},f.owner(),true).entries.length===0&&
   f.g.inv.getDiff({entries:late},f.last.lastInv,true).entries.length===0);
  f.close();
 }
 {
  const f=pickpocket(),newest=[{baseId:SWORD,count:3,name:'Newest target'}];
  f.target([]);f.model.inventory={entries:[{baseId:SWORD,count:1001,name:'Old target'}]};f.open();await f.advance(10);
  f.model.inventory={entries:newest};f.open();await f.advance(110);
  yes('a newer open request supersedes the old waiter and opens only the newest exact target once',
   f.opens===1&&f.g.inv.getDiff({entries:newest},f.mark(),true).entries.length===0);
  f.close();
 }
 {
  const f=pickpocket(),same=[{baseId:SWORD,count:1001,name:'Shared target job'}];
  f.target([]);f.model.inventory={entries:same};f.open();await f.advance(10);
  const scope=f.g.nativeApply.scopes.find(s=>s.id===0x99);f.open();await f.advance(110);
  yes('a superseded waiter cannot cancel the replacement request sharing its still-current job',
   f.opens===1&&scope.state()==='drained'&&f.g.inv.getDiff({entries:same},f.mark(),true).entries.length===0);
  f.close();
 }
 {
  const f=pickpocket();f.target([]);f.model.inventory={entries:[{baseId:SWORD,count:1001}]};
  const nativeAdd=f.sp.TESModPlatform.addItemEx;
  f.sp.TESModPlatform.addItemEx=(ref,...args)=>{nativeAdd(ref,...args);if(ref.getFormID()===0x99)throw Error('unknown native outcome');};
  f.open();await f.advance(110);
  yes('an applied-then-thrown partial target job is not replayed or mistaken for a full pack',
   f.opens===0&&f.g.nativeApply.scopes.some(s=>s.id===0x99&&s.state()==='uncertain')&&
   f.mark().entries.reduce((n,e)=>n+e.count,0)===1);
  f.close();
 }
 console.log(pass+' passed, '+fail+' failed');process.exitCode=fail?1:0;
})().catch(error=>{console.error(error.stack);process.exitCode=1;});
