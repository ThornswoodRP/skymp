'use strict';
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),cp=require('node:child_process');
const source=fs.readFileSync(path.join(__dirname,'../../../skyrim-platform/src/platform_se/skyrim_platform/InventoryApplyApi.cpp'),'utf8');
const work=fs.mkdtempSync(path.join(os.tmpdir(),'thornswood-inventory-scope-'));
try {
 for(const [part,file] of [['scope','InventoryScopeUnderTest.inc'],['state','InventoryStateUnderTest.inc'],['factory','InventoryFactoryUnderTest.inc']]){
  const start=source.indexOf('// ==== inventory apply '+part+' ===='),end=source.indexOf('// ==== end inventory apply '+part+' ====',start);
  if(start<0||end<0)throw Error('actual inventory '+part+' boundary missing');
  fs.writeFileSync(path.join(work,file),source.slice(start,end));
 }
 const {command,env}=require('./cxx').compiler(),fixture=path.join(__dirname,'test_inventory_apply_scope_native.cpp'),exe=path.join(work,process.platform==='win32'?'scope.exe':'scope');
 const args=process.platform==='win32'?['/nologo','/EHsc','/std:c++17','/I'+work,'/Fe:'+exe,'/Fo:'+path.join(work,'scope.obj'),fixture]:['-std=c++17','-I'+work,fixture,'-o',exe];
 const build=cp.spawnSync(command,args,{cwd:work,encoding:'utf8',env});
 process.stdout.write(build.stdout||'');process.stderr.write(build.stderr||'');
 if(build.error)throw build.error;
 if(build.status!==0)process.exitCode=build.status||1;
 else {const run=cp.spawnSync(exe,[],{encoding:'utf8'});process.stdout.write(run.stdout||'');process.stderr.write(run.stderr||'');if(run.status!==0)process.exitCode=run.status||1;}
}finally{
 const parent=fs.realpathSync(os.tmpdir()),target=fs.realpathSync(work);
 if(path.dirname(target).toLowerCase()!==parent.toLowerCase()||!path.basename(target).startsWith('thornswood-inventory-scope-'))throw Error('Unsafe cleanup target');
 fs.rmSync(target,{recursive:true,force:true});
}
