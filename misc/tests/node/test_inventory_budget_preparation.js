'use strict';
// Actual inventory.ts preparation, with a narrow native batch contract shim.
// Native scheduler execution/fairness has separate actual-header tests.
const fs=require('node:fs'),path=require('node:path'),Module=require('node:module');
const file=path.join(__dirname,'test_inventory_differences_close.js'),source=fs.readFileSync(file,'utf8');
const end=source.indexOf('// A fine sword the server holds:');if(end<0)throw Error('actual fixture boundary missing');
const fixture=new Module(file,module);fixture.filename=file;fixture.paths=Module._nodeModulePaths(__dirname);
fixture._compile(source.slice(0,end)+'\nmodule.exports={makeGame,SWORD,BOOTS,GOLD};',file);
const {makeGame,SWORD,BOOTS,GOLD}=fixture.exports;let pass=0,fail=0;
function yes(name,ok){console.log((ok?'PASS ':'FAIL ')+name);ok?pass++:fail++;}
for(const baseId of [SWORD,BOOTS])for(const count of [1001,100000,0xffffffff]){
 const g=makeGame(),jobs=[];let calls=0,threw=false;
 const add=g.sp.TESModPlatform.addItemEx;
 g.sp.TESModPlatform.addItemEx=(...args)=>{if(++calls>32)throw Error('per-update preparation exceeded32 unit calls');return add(...args);};
 g.sp.createInventoryApply=(referenceId)=>{
  const handle={state:()=> 'pending',isCurrent:()=>true,isReady:()=>true,cancel(){},referenceId,
   submit(descriptors){handle.descriptors=descriptors;jobs.push(handle);}};
  return handle;
 };
 try{g.beginApply([{baseId,count,name:'Prepared piece',health:1.4}]);g.advance();g.advance();}catch(e){threw=true;}
 yes('count'+count+' prepares without an unbounded unit loop',!threw&&calls<=16);
 yes('count'+count+' schedules one compact full-count descriptor',jobs.length===1&&jobs[0].referenceId===0x14&&jobs[0].descriptors.length===1&&jobs[0].descriptors[0].count===count);
 yes('count'+count+' logging is bounded by entries rather than units',g.console.length<=2);
}
for(const count of [Infinity,NaN,-1,1.5,0x100000000,'1001',null,undefined]){
 const g=makeGame();let jobs=0;g.sp.createInventoryApply=()=>{jobs++;throw Error('invalid target reached native batch');};
 let threw=false;try{g.beginApply([{baseId:SWORD,count}]);}catch(e){threw=true;}
 yes('invalid count refuses before any batch/reset/mutation',!threw&&jobs===0&&g.calls.length===0);
}
console.log(pass+' passed, '+fail+' failed');process.exitCode=fail?1:0;
