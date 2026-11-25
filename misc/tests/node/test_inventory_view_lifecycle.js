'use strict';
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),Module=require('node:module');
const ts=require('../../../skymp5-client/node_modules/typescript');
const file=path.join(__dirname,'test_inventory_differences_close.js'),source=fs.readFileSync(file,'utf8'),end=source.indexOf('// A fine sword the server holds:');
const fixture=new Module(file,module);fixture.filename=file;fixture.paths=Module._nodeModulePaths(__dirname);fixture._compile(source.slice(0,end)+'\nmodule.exports={makeGame,SWORD};',file);
const {makeGame,SWORD}=fixture.exports;let pass=0,fail=0;
function yes(label,ok){console.log((ok?'PASS ':'FAIL ')+label);ok?pass++:fail++;}
{
 const g=makeGame(),box={exports:{},require:name=>name==='skyrimPlatform'?g.sp:name.endsWith('/inventory')?g.inv:{}};
 const model=fs.readFileSync(path.join(__dirname,'../../../skymp5-client/src/view/modelApplyUtils.ts'),'utf8');
 vm.runInNewContext(ts.transpileModule(model,{compilerOptions:{module:ts.ModuleKind.CommonJS,target:ts.ScriptTarget.ES2018}}).outputText,box);
 const target=[{baseId:SWORD,count:1001,name:'Streamed pack',health:1.4}];
 box.exports.ModelApplyUtils.applyModelInventory(g.pc,{entries:target});g.advance();
 yes('a single model update creates a compact continuing job',g.calls.length===16&&g.nativeApply.creations===1);
 g.nativeApply.drain();
 yes('a model job completes without another snapshot or owner polling',g.calls.length===1001&&g.diff(target).length===0&&g.nativeApply.creations===1);
}
const viewFile=path.join(__dirname,'../../../skymp5-client/src/view/formView.ts'),viewSource=fs.readFileSync(viewFile,'utf8');
const ast=ts.createSourceFile(viewFile,viewSource,ts.ScriptTarget.Latest,true),cls=ast.statements.find(ts.isClassDeclaration),member=cls.members.find(m=>m.name?.getText(ast)==='destroy');
if(!member)throw Error('actual FormView.destroy missing');
const body=ts.transpileModule('class Subject {'+member.getText(ast)+'}',{compilerOptions:{target:ts.ScriptTarget.ES2018}}).outputText+'\nreturn Subject.prototype.destroy;';
{
 const g=makeGame();g.beginApply([{baseId:SWORD,count:1001}]);g.advance();
 const destroy=new Function('cancelInventoryApplication','once',body)(g.nativeApply.coordinator.cancelInventoryApplication,()=>{throw Error('placed reference must not be deleted');});
 destroy.call({refrId:0x14,adoptedRefrId:0,removeNickname(){}});
 yes('actual view destruction cancels inventory synchronously',g.nativeApply.scopes[0].state()==='cancelled');
 g.advance();yes('destroyed view has no remaining native additions',g.calls.length===16);
}
{
 const g=makeGame();g.beginApply([{baseId:SWORD,count:1001}]);g.advance();
 const destroy=new Function('cancelInventoryApplication','once',body)(g.nativeApply.coordinator.cancelInventoryApplication,()=>{});
 destroy.call({refrId:0x15,adoptedRefrId:0,removeNickname(){}});g.advance();
 yes('destroying another reference does not cancel this inventory job',g.calls.length===32&&g.nativeApply.scopes[0].state()==='pending');
}
{
 const order=[];
 const destroy=new Function('cancelInventoryApplication','once',body)(id=>order.push(['cancel',id]),(name,fn)=>order.push(['defer',name,fn]));
 destroy.call({refrId:0xff000010,adoptedRefrId:0,removeNickname(){}});
 yes('runtime view cancellation precedes deferred native deletion/reuse',order.length===2&&order[0][0]==='cancel'&&order[0][1]===0xff000010&&order[1][0]==='defer');
}
console.log(pass+' passed, '+fail+' failed');process.exitCode=fail?1:0;
