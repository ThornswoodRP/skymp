'use strict';
// Hosted CTest compiles the actual inventory scheduler header. No Skyrim mock
// substitutes its scheduling, cancellation or budget implementation.
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),cp=require('node:child_process');
const work=fs.mkdtempSync(path.join(os.tmpdir(),'thornswood-inventory-budget-'));
try{
 const fixture=path.join(__dirname,'test_inventory_budget_native.cpp'),exe=path.join(work,process.platform==='win32'?'budget.exe':'budget');
 const {command,env}=require('./cxx').compiler(),headers=path.join(__dirname,'../../../viet/include');
 const args=process.platform==='win32'?['/nologo','/EHsc','/std:c++17','/I'+headers,'/Fe:'+exe,'/Fo:'+path.join(work,'budget.obj'),fixture]:['-std=c++17','-I'+headers,fixture,'-o',exe];
 const build=cp.spawnSync(command,args,{encoding:'utf8',cwd:work,env});process.stdout.write(build.stdout||'');process.stderr.write(build.stderr||'');
 if(build.error)throw build.error;
 if(build.status!==0)process.exitCode=build.status===null?1:build.status;
 else{const run=cp.spawnSync(exe,[],{encoding:'utf8'});process.stdout.write(run.stdout||'');process.stderr.write(run.stderr||'');if(run.error)throw run.error;if(run.status!==0)process.exitCode=run.status===null?1:run.status;}
}finally{
 const parent=fs.realpathSync(os.tmpdir()),owned=fs.realpathSync(work);
 if(path.dirname(owned).toLowerCase()!==parent.toLowerCase()||!path.basename(owned).startsWith('thornswood-inventory-budget-'))throw Error('Unsafe fixture cleanup target');
 fs.rmSync(owned,{recursive:true,force:true});
}
