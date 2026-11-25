'use strict';
// Hosted CTest compiles the actual inventory scheduler header. No Skyrim mock
// substitutes its scheduling, cancellation or budget implementation.
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),cp=require('node:child_process');
const work=fs.mkdtempSync(path.join(os.tmpdir(),'thornswood-inventory-budget-'));
try{
 const fixture=path.join(__dirname,'test_inventory_budget_native.cpp');
 const {command,env}=require('./cxx').compiler(),headers=path.join(__dirname,'../../../viet/include'),native=path.join(__dirname,'../../../skyrim-platform/src/platform_se/skyrim_platform');
 const pump=fs.readFileSync(path.join(native,'InventoryApplyPump.h'),'utf8');
 function run(label,source,expected){
  fs.writeFileSync(path.join(work,'InventoryPumpUnderTest.h'),source);
  const exe=path.join(work,label+(process.platform==='win32'?'.exe':''));
  const args=process.platform==='win32'?['/nologo','/EHsc','/std:c++17','/I'+headers,'/I'+native,'/I'+work,'/Fe:'+exe,'/Fo:'+path.join(work,label+'.obj'),fixture]:['-std=c++17','-I'+headers,'-I'+native,'-I'+work,fixture,'-o',exe];
  const build=cp.spawnSync(command,args,{encoding:'utf8',cwd:work,env,timeout:120000});process.stdout.write(build.stdout||'');process.stderr.write(build.stderr||'');
  if(build.error)throw build.error;
  if(build.status!==0)throw Error('Actual native fixture failed to compile: '+label);
  const result=cp.spawnSync(exe,[],{encoding:'utf8',timeout:15000});process.stdout.write(result.stdout||'');process.stderr.write(result.stderr||'');
  if(result.error)throw result.error;
  if(result.status!==expected)throw Error(label+' returned '+result.status+', expected '+expected);
  if(expected===77&&!result.stdout.includes('COUNTERFACTUAL old cancellation strands'))throw Error('Old-order control failed for another reason');
 }
 run('budget',pump,0);
 const start=pump.indexOf('    // ==== retire inventory pump ticket ===='),end=pump.indexOf('    // ==== end retire inventory pump ticket ====',start);
 if(start<0||end<0)throw Error('Actual pump cancellation boundary missing');
 const after=pump.indexOf('\n',end)+1,block=pump.slice(start,after),without=pump.slice(0,start)+pump.slice(after);
 if((without.match(/    cancelJobs\(\);/g)||[]).length!==1)throw Error('Actual pump cancellation handoff changed');
 run('old-order',without.replace('    cancelJobs();','    cancelJobs();\n'+block),77);
}finally{
 const parent=fs.realpathSync(os.tmpdir()),owned=fs.realpathSync(work);
 if(path.dirname(owned).toLowerCase()!==parent.toLowerCase()||!path.basename(owned).startsWith('thornswood-inventory-budget-'))throw Error('Unsafe fixture cleanup target');
 fs.rmSync(owned,{recursive:true,force:true});
}
