'use strict';
// Narrow native API stand-in for existing TS metadata/rename tests. It records
// compact descriptors and advances them only on explicit fixture frames. The
// real native pump, budgets, identity and failure behavior are tested against
// their actual C++ headers/source in the separate hosted native fixtures.
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const ts=require('../../../skymp5-client/node_modules/typescript');
exports.install=function(sp,reference){
 const listeners={update:[]},scopes=[];let frame=0,creations=0;
 const originalGet=sp.Game.getFormEx;
 const openMenus=new Set();
 sp.Ui=sp.Ui||{isMenuOpen:name=>openMenus.has(name)};
 sp.Game.getFormEx=id=>id===reference.getFormID()?reference:originalGet(id);
 sp.ObjectReference.from=value=>value instanceof sp.ObjectReference?value:null;
 sp.on=(name,fn)=>{(listeners[name]||=[]).push(fn);};
 sp.getInventoryQueueFence=()=>{const captured=frame;return()=>frame>captured;};
 sp.cancelInventoryApply=id=>{for(const scope of scopes)if(scope.id===id)scope.cancel();};
 sp.createInventoryApply=id=>{
  sp.cancelInventoryApply(id);creations++;
  const scope={id,value:'preparing',descriptors:[],at:0,left:0,refresh:false,
   isCurrent:()=>scope.value!=='cancelled',isReady:()=>true,state:()=>scope.value,
   cancel(){scope.value='cancelled';},
   submit(descriptors){scope.descriptors=descriptors.map(e=>({...e}));scope.value=descriptors.length?'pending':'drained';scope.left=descriptors[0]?.count||0;}
  };scopes.push(scope);return scope;
 };
 const coordinatorFile=path.join(__dirname,'../../../skymp5-client/src/sync/inventoryApplyCoordinator.ts');
 const menuFile=path.join(__dirname,'../../../skymp5-client/src/sync/inventoryMenus.ts'),menuBox={exports:{},require:name=>{if(name==='skyrimPlatform')return sp;throw Error('Unexpected menu dependency '+name);}};
 vm.runInNewContext(ts.transpileModule(fs.readFileSync(menuFile,'utf8'),{compilerOptions:{module:ts.ModuleKind.CommonJS,target:ts.ScriptTarget.ES2018}}).outputText,menuBox);
 const menuPolicy=menuBox.exports;
 const sandbox={exports:{},require:name=>{if(name==='skyrimPlatform')return sp;if(name==='./inventoryMenus')return menuPolicy;throw Error('Unexpected coordinator dependency '+name);}};
 vm.runInNewContext(ts.transpileModule(fs.readFileSync(coordinatorFile,'utf8'),{compilerOptions:{module:ts.ModuleKind.CommonJS,target:ts.ScriptTarget.ES2018}}).outputText,sandbox);
 const coordinator=sandbox.exports;
 function advance(){
  frame++;
  for(const fn of listeners.update)fn();
  let budget=16;
  for(const scope of scopes){
   while(scope.value==='pending'){
    if(scope.id===0x14&&menuPolicy.ownerInventoryMenuBlocked())break;
    const e=scope.descriptors[scope.at];
    if(scope.refresh){
     if(budget<1)break;budget--;sp.Actor.from(reference)?.queueNiNodeUpdate();scope.refresh=false;
     if(++scope.at===scope.descriptors.length){scope.value='drained';break;}
     scope.left=scope.descriptors[scope.at].count;continue;
    }
    const cost=e.worn||e.wornLeft?2:1;if(budget<cost)break;budget-=cost;
    const n=Math.min(scope.left,e.quantityPerUnit);
    if(e.worn||e.wornLeft)sp.TESModPlatform.pushWornState(e.worn,e.wornLeft);
    try{sp.TESModPlatform.addItemEx(reference,sp.Game.getFormEx(e.baseId),n,e.health,
     e.enchantmentId?sp.Enchantment.from(sp.Game.getFormEx(e.enchantmentId)):null,e.maxCharge,e.removeEnchantmentOnUnequip,e.chargePercent,e.name,e.soul,
     e.poisonId?sp.Potion.from(sp.Game.getFormEx(e.poisonId)):null,e.poisonCount);
    }catch(error){scope.value='uncertain';break;}
    scope.left-=n;
    if(!scope.left){
     if(e.worn||e.wornLeft)scope.refresh=true;
     else if(++scope.at===scope.descriptors.length){scope.value='drained';break;}
     else scope.left=scope.descriptors[scope.at].count;
    }
   }
  }
 }
 function drain(){let turns=0;while(coordinator.inventoryApplyPending(reference.getFormID())){if(++turns>20000)throw Error('Fixture inventory never settled');advance();}}
 return {coordinator,menuPolicy,advance,drain,scopes,menu(name,open=true){open?openMenus.add(name):openMenus.delete(name);},emit(name){for(const fn of listeners[name]||[])fn();},get creations(){return creations;},get frame(){return frame;}};
};
