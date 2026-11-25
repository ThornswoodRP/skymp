'use strict';
const fs=require('node:fs'),path=require('node:path'),Module=require('node:module'),vm=require('node:vm');
const ts=require('../../../skymp5-client/node_modules/typescript');
const file=path.join(__dirname,'test_inventory_differences_close.js'),source=fs.readFileSync(file,'utf8'),end=source.indexOf('// A fine sword the server holds:');
const fixture=new Module(file,module);fixture.filename=file;fixture.paths=Module._nodeModulePaths(__dirname);fixture._compile(source.slice(0,end)+'\nmodule.exports={makeGame,SWORD};',file);
const {makeGame,SWORD}=fixture.exports;let pass=0,fail=0;
function yes(label,ok){console.log((ok?'PASS ':'FAIL ')+label);ok?pass++:fail++;}
const menus=['InventoryMenu','FavoritesMenu','MagicMenu','ContainerMenu','Crafting Menu','Lockpicking Menu'];
for(const menu of menus){
 const g=makeGame();g.lists=[{baseId:SWORD,count:1,name:'Must remain'}];g.nativeApply.menu(menu);g.beginApply([{baseId:SWORD,count:20,name:'New target'}]);
 for(let i=0;i<3;i++)g.advance();
 yes(menu+' defers owner preparation/removal without discarding the target',g.calls.length===0&&g.lists[0].name==='Must remain'&&g.nativeApply.scopes[0].descriptors.length===0&&g.nativeApply.coordinator.inventoryApplyPending(0x14));
 g.nativeApply.menu(menu,false);g.nativeApply.drain();
 yes(menu+' resumes one fresh reconciliation after close',g.diff([{baseId:SWORD,count:20,name:'New target'}]).length===0&&g.nativeApply.creations===1);
}
{
 const g=makeGame();g.beginApply([{baseId:SWORD,count:40}]);g.advance();const before=g.calls.length;
 g.nativeApply.menu('ContainerMenu');g.advance();g.advance();
 yes('an admitted owner job pauses inside a container without replay or cancellation',before===16&&g.calls.length===16&&g.nativeApply.scopes[0].state()==='pending');
 g.nativeApply.menu('ContainerMenu',false);g.nativeApply.drain();
 yes('the admitted job resumes its remaining counter exactly once',g.calls.length===40&&g.diff([{baseId:SWORD,count:40}]).length===0&&g.nativeApply.creations===1);
}
{
 const g=makeGame();g.nativeApply.menu('ContainerMenu');let prepared=0;
 g.nativeApply.coordinator.requestInventoryApply(0x15,'npc',()=>{prepared++;return[];});g.advance();
 yes('owner menu deferral does not freeze NPC/reference preparation',prepared===1);
}
{
 const g=makeGame(),policy=g.nativeApply.menuPolicy;
 const native=fs.readFileSync(path.join(__dirname,'../../../skyrim-platform/src/platform_se/skyrim_platform/InventoryApplyMenus.h'),'utf8');
 const nativeNames=[...native.matchAll(/"([^"]+)"/g)].map(m=>m[1]);
 yes('actual native and TS menu predicates share the same exact names',JSON.stringify(nativeNames)===JSON.stringify([...policy.badInventoryMenus,'Lockpicking Menu']));
 const equipment=fs.readFileSync(path.join(__dirname,'../../../skymp5-client/src/sync/equipment.ts'),'utf8'),box={exports:{},require:name=>name==='skyrimPlatform'?g.sp:name==='./inventoryMenus'?policy:g.inv};
 vm.runInNewContext(ts.transpileModule(equipment,{compilerOptions:{module:ts.ModuleKind.CommonJS,target:ts.ScriptTarget.ES2018}}).outputText,box);
 g.nativeApply.menu('Lockpicking Menu');
 yes('equipment keeps its existing public bad-menu semantics',!box.exports.isBadMenuShown()&&policy.ownerInventoryMenuBlocked());
}
console.log(pass+' passed, '+fail+' failed');process.exitCode=fail?1:0;
