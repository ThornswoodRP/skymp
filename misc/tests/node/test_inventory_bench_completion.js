'use strict';
// Real RemoteServer + real inventory.ts + real TS coordinator. Only the new
// native scope/pump API is a narrow stand-in; its C++ implementation has
// separate actual-source hosted tests.
const fs=require('node:fs'),path=require('node:path'),Module=require('node:module'),vm=require('node:vm');
const ts=require('../../../skymp5-client/node_modules/typescript');
const file=path.join(__dirname,'test_inventory_differences_close.js'),source=fs.readFileSync(file,'utf8'),end=source.indexOf('// A fine sword the server holds:');
const fixture=new Module(file,module);fixture.filename=file;fixture.paths=Module._nodeModulePaths(__dirname);fixture._compile(source.slice(0,end)+'\nmodule.exports={makeGame,SWORD};',file);
const {makeGame,SWORD}=fixture.exports;let pass=0,fail=0;
function yes(label,ok){console.log((ok?'PASS ':'FAIL ')+label);ok?pass++:fail++;}
function game(){
 const g=makeGame(),sp=g.sp,once=[],waits=[];let opens=0,seated=false,paused=false;
 sp.storage.worldModel={forms:[{refrId:0xff000014}],playerCharacterFormIdx:0,playerCharacterRefrId:0xff000014};
 sp.storage.idManager={getId:id=>id};
 sp.Game.getPlayer=()=>g.pc;g.pc.getFurnitureReference=()=>seated?target:null;
 sp.Ui={isMenuOpen:()=>false};sp.Utility={isInMenuMode:()=>paused,wait:()=>new Promise(resolve=>waits.push(resolve)),waitMenuMode:()=>Promise.resolve()};
 sp.once=(name,fn)=>{if(name!=='update')throw Error('unexpected event '+name);once.push(fn);};
 sp.on('update',()=>{for(const fn of once.splice(0))fn();for(const resolve of waits.splice(0))resolve();});
 const target=new sp.ObjectReference(0x99);target.getBaseObject=()=>({getType:()=>40});target.activate=()=>{opens++;seated=true;};
 const get=sp.Game.getFormEx;sp.Game.getFormEx=id=>id===0x99?target:get(id);sp.FormType.Furniture=40;
 const remoteFile=path.join(__dirname,'../../../skymp5-client/src/services/services/remoteServer.ts');
 const sandbox={exports:{},Date:{now:()=>100},Promise,require(name){
  if(name==='skyrimPlatform')return sp;
  if(name.endsWith('/inventory'))return g.inv;
  if(name.endsWith('/inventoryApplyCoordinator'))return g.nativeApply.coordinator;
  if(name.endsWith('/clientListener'))return{ClientListener:class{}};
  if(name.endsWith('/equipment'))return{isBadMenuShown:()=>paused};
  if(name.endsWith('/nameof'))return{nameof:value=>value};
  if(name.endsWith('/worldViewMisc'))return{remoteIdToLocalId:id=>id,getViewFromStorage:()=>null};
  if(name.endsWith('/logging'))return{logError(){},logTrace(){}};
  if(name.endsWith('/messages'))return{MsgType:{Activate:1}};
  return{};
 }};
 vm.runInNewContext(ts.transpileModule(fs.readFileSync(remoteFile,'utf8'),{compilerOptions:{module:ts.ModuleKind.CommonJS,target:ts.ScriptTarget.ES2018}}).outputText,sandbox);
 const remote=Object.create(sandbox.exports.RemoteServer.prototype);
 Object.assign(remote,{sp,controller:{emitter:{emit(){}}},ownerSpawnEpoch:0,raceMenuRequest:0,numSetInventory:0,teleportsOnTheirWay:new Map()});
 return{g,sp,remote,get opens(){return opens;},set paused(value){paused=value;},
  async advance(n){for(let i=0;i<n;i++){g.advance();for(let micro=0;micro<12;micro++)await Promise.resolve();}},
  open(){remote.onOpenContainerMessage({message:{target:0x99}});},
  target(entries){sp.storage.pcInv={entries};remote.ownerInventory=sp.storage.pcInv;},
  stop(){seated=false;}
 };
}
(async()=>{
 {
  const f=game(),target=[{baseId:SWORD,count:1001,name:'Bench stock',health:1.4}];f.target(target);f.open();await f.advance(8);
  yes('a bench does not open when only the first bounded native chunk ran',f.opens===0&&f.g.calls.length>0&&f.g.calls.length<1001);
  await f.advance(90);
  yes('the actual bench handler opens once after the whole job and fresh equality check',f.opens===1&&f.g.calls.length===1001&&f.g.diff(target).length===0);
  yes('bench preparation uses one compact job without five-second owner retries',f.g.nativeApply.creations===1);
  f.stop();await f.advance(2);
 }
 {
  const f=game();f.target([{baseId:SWORD,count:1001}]);f.open();await f.advance(8);const before=f.g.calls.length;
  f.remote.onSetInventoryMessage({message:{inventory:{entries:[{baseId:SWORD,count:20}]}}});await f.advance(8);
  yes('a newer owner snapshot cancels the old job before another native chunk',f.g.calls.length===before);
  yes('a cancelled old-target job cannot open the requested bench',f.opens===0);
 }
 {
  const f=game();f.target([{baseId:SWORD,count:1001}]);f.open();await f.advance(8);const before=f.g.calls.length;
  f.remote.cancelOwnerSpawn();await f.advance(8);
  yes('an owner/connection epoch change stops the job and delayed activation',f.opens===0&&f.g.calls.length===before);
 }
 {
  const f=game();f.target([{baseId:SWORD,count:1001}]);f.open();await f.advance(8);f.paused=true;await f.advance(90);
  yes('a menu opened during completion prevents delayed activation',f.opens===0);
 }
 {
  const f=game();f.target([{baseId:SWORD,count:1001}]);f.open();await f.advance(8);const before=f.g.calls.length;
  f.remote.onDeathStateContainerMessage({message:{tIsDead:{idx:0,propName:'isDead',data:'false'}}});
  yes('malformed death data does not cancel a valid inventory job',f.g.nativeApply.scopes[0].state()==='pending');
  for(const data of [true,false])f.remote.onDeathStateContainerMessage({message:{tIsDead:{idx:0,propName:'isDead',data}}});
  yes('death and immediate revival retire work synchronously before queued property callbacks',f.g.nativeApply.scopes[0].state()==='cancelled');
  await f.advance(8);yes('a death/revival ABA cannot release old bench readiness or add more units',f.opens===0&&f.g.calls.length===before);
 }
 {
  const f=game();f.target([{baseId:SWORD,count:1001}]);f.open();await f.advance(8);const before=f.g.calls.length;
  f.sp.getInventoryQueueFence=()=>()=>false;
  f.g.nativeApply.emit('preLoadGame');await f.advance(8);
  yes('load cancellation stops an old activation even if its queue fence is cleared',f.opens===0&&f.g.calls.length===before&&!f.g.nativeApply.coordinator.inventoryApplyPending(0x14));
 }
 console.log(pass+' passed, '+fail+' failed');process.exitCode=fail?1:0;
})().catch(error=>{console.error(error.stack);process.exitCode=1;});
