'use strict';
// Execute the actual TS inventory and coordinator with explicit native frames.
// Native budget/performance claims come only from the separate C++ fixtures
// and eventual Dev runtime acceptance, not this API stand-in.
const fs=require('node:fs'),path=require('node:path'),Module=require('node:module');
const file=path.join(__dirname,'test_inventory_differences_close.js'),source=fs.readFileSync(file,'utf8'),end=source.indexOf('// A fine sword the server holds:');
if(end<0)throw Error('actual fixture boundary missing');
const fixture=new Module(file,module);fixture.filename=file;fixture.paths=Module._nodeModulePaths(__dirname);fixture._compile(source.slice(0,end)+'\nmodule.exports={makeGame,SWORD,BOOTS,GOLD};',file);
const {makeGame,SWORD,GOLD}=fixture.exports;let pass=0,fail=0;
function yes(label,ok){console.log((ok?'PASS ':'FAIL ')+label);ok?pass++:fail++;}
{
 const g=makeGame(),target=[{baseId:SWORD,count:100000,name:'Whole job',health:1.4}];let reads=0;
 const read=g.sp.getExtraContainerChanges;g.sp.getExtraContainerChanges=(...args)=>{reads++;return read(...args);};
 g.beginApply(target);const full=g.nativeApply.coordinator.inventoryApplyFence(0x14),position=g.sp.getInventoryQueueFence();
 yes('request does no unit preparation/mutation synchronously',g.calls.length===0&&reads===0);
 g.advance();
 yes('a first native queue fence is not whole-job completion',position()&&!full()&&g.calls.length===16);
 const nativeScope=g.nativeApply.scopes[0];
 for(let i=0;i<3;i++){g.beginApply(target);g.advance();}
 yes('same target shares one job and does not rediff a partial pack',g.nativeApply.creations===1&&reads===1&&nativeScope.descriptors.length===1);
 let frames=0;while(!full()){if(++frames>7000)throw Error('whole job did not finish');g.advance();}
 yes('one complete reconciliation drains without five-second retries',g.calls.length===100000&&g.nativeApply.creations===1&&g.diff(target).length===0);
 yes('bounded descriptor logging does not grow per unit',g.console.length===1);
}
{
 const g=makeGame();g.beginApply([{baseId:SWORD,count:1001,name:'Same piece'}]);g.advance();
 const before=g.calls.length;g.beginApply([{baseId:SWORD,count:20,name:'Same piece'}]);g.advance();
 g.nativeApply.drain();
 yes('replacement cancels old remaining units and computes a fresh difference',before===16&&g.calls.filter(x=>x[0]==='addItemEx').length===20&&g.diff([{baseId:SWORD,count:20,name:'Same piece'}]).length===0);
 yes('replacement has one latest target rather than per-unit queued callbacks',g.nativeApply.creations===2&&g.nativeApply.scopes[0].value==='cancelled');
}
{
 const g=makeGame();g.beginApply([{baseId:SWORD,count:1001}]);g.advance();
 const fence=g.nativeApply.coordinator.inventoryApplyFence(0x14),count=g.calls.length;
 g.nativeApply.coordinator.cancelInventoryApplication(0x14);
 yes('cancel does not acknowledge preceding queued work early',!fence());
 g.advance();yes('cancellation stops remaining units and the terminal fence settles',fence()&&g.calls.length===count);
}
{
 const g=makeGame();g.beginApply([{baseId:SWORD,count:1001}]);
 g.nativeApply.scopes[0].cancel();g.advance();g.advance();
 yes('retired native scope cannot reset or prepare a reused reference',g.calls.length===0&&g.nativeApply.scopes[0].descriptors.length===0);
}
{
 const g=makeGame();let first=true;const add=g.sp.TESModPlatform.addItemEx;
 g.sp.TESModPlatform.addItemEx=(...args)=>{add(...args);if(first){first=false;throw Error('applied then throw');}};
 const target=[{baseId:SWORD,count:20,name:'Unknown delivery'}];g.beginApply(target);g.advance();g.advance();
 yes('uncertain native outcome is terminal and is not silently replayed',g.calls.length===1&&g.nativeApply.scopes[0].state()==='uncertain');
 g.beginApply(target);g.nativeApply.drain();
 yes('a new plan reads actual state after uncertainty and adds only remaining units',g.calls.length===20&&g.diff(target).length===0);
}
{
 const g=makeGame(),read=g.sp.getExtraContainerChanges;g.sp.getExtraContainerChanges=()=>{throw Error('actual pack unavailable');};
 g.beginApply([{baseId:SWORD,count:10}]);g.advance();g.advance();
 yes('unavailable actual inventory terminates preparation without a native add',g.calls.length===0&&g.nativeApply.scopes[0].value==='cancelled');
 g.sp.getExtraContainerChanges=read;g.beginApply([{baseId:SWORD,count:10}]);g.nativeApply.drain();
 yes('restored actual observation permits a separate fresh plan',g.calls.length===10);
}
{
 const g=makeGame();g.beginApply([{baseId:SWORD,count:0xffffffff}]);g.advance();g.nativeApply.coordinator.cancelAllInventoryApplications();g.advance();
 yes('maximum uint32 count remains compact and cancellation does not enumerate it',g.calls.length===16&&g.nativeApply.scopes[0].descriptors[0].count===0xffffffff);
}
{
 const g=makeGame();g.beginApply([{baseId:GOLD,count:100000}]);g.nativeApply.drain();
 yes('bulk forms retain one native addition rather than a gear unit loop',g.calls.length===1&&g.calls[0][2]===100000);
}
{
 const g=makeGame(),coordinator=g.nativeApply.coordinator;let first=0,later=0;
 coordinator.requestInventoryApply(0x15,'paused',()=>{first++;return[];});g.nativeApply.scopes[0].isReady=()=>false;
 coordinator.requestInventoryApply(0x16,'later',()=>{later++;return[];});
 g.advance();g.advance();
 yes('paused TS preparation rotates so a later reference is prepared promptly',first===0&&later===1);
}
{
 const g=makeGame();g.beginApply([{baseId:SWORD,count:1001}]);g.advance();
 const full=g.nativeApply.coordinator.inventoryApplyFence(0x14);
 g.nativeApply.emit('preLoadGame');
 yes('the captured target fence exposes lifecycle retirement separately from completion',!full.isCurrent());
 g.advance();yes('real preLoad handler removes desired work rather than resubmitting it',!g.nativeApply.coordinator.inventoryApplyPending(0x14)&&g.calls.length===16);
}
console.log(pass+' passed, '+fail+' failed');process.exitCode=fail?1:0;
