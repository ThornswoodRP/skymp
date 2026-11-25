'use strict';
// Execute the actual workflow shell with only Docker and the one-second wait
// replaced. This proves argv/deadline behavior, not real MongoDB startup.
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),cp=require('node:child_process');
const root=path.resolve(__dirname,'../../..');
const workflow=fs.readFileSync(process.env.MONGO_WORKFLOW||path.join(root,'.github/workflows/linux-build-base.yml'),'utf8').replace(/\r/g,'');
const fast=fs.readFileSync(path.join(root,'.github/workflows/thornswood-pr-windows.yml'),'utf8');
const bash=process.platform==='win32'?path.join(process.env.ProgramFiles||'C:/Program Files','Git/bin/bash.exe'):'/bin/bash';
let pass=0,fail=0;
function yes(name,ok){console.log((ok?'PASS ':'FAIL ')+name);ok?pass++:fail++;}
function step(name){
 const lines=workflow.split('\n'),matches=[];
 lines.forEach((line,i)=>{if(line.trim()==='- name: '+name)matches.push(i);});
 if(matches.length!==1)throw Error('Expected one workflow step: '+name);
 const start=matches[0],indent=lines[start].search(/\S/),body=[];let collecting=false,condition='';
 for(let i=start+1;i<lines.length;i++){
  const line=lines[i];if(line.trim()&&line.search(/\S/)<=indent)break;
  if(!collecting){if(line.trim().startsWith('if:'))condition=line.trim();if(line.trim()==='run: |')collecting=true;continue;}
  if(line.trim()&&line.search(/\S/)<indent+4)break;
  body.push(line.slice(indent+4));
 }
 if(!collecting||!body.length)throw Error('Workflow shell missing: '+name);
 const script=body.join('\n').replace(/\$\{\{\s*github\.workspace\s*\}\}/g,'/workspace')
  .replace(/\$\{\{\s*env\.BUILD_TYPE\s*\}\}/g,'RelWithDebInfo');
 if(/\$\{\{/.test(script))throw Error('Unmodeled workflow expression');
 return {script,condition};
}
const ready=step('Start MongoDB for the tests'),test=step('Test');
const work=fs.mkdtempSync(path.join(os.tmpdir(),'thornswood-mongo-probe-'));
const fake=`
docker() {
  local probe_command="$1"; shift
  printf '%s' "$probe_command" >> "$PROBE_TRACE"
  printf '\\t%s' "$@" >> "$PROBE_TRACE"; printf '\\n' >> "$PROBE_TRACE"
  case "$probe_command" in
    run) echo fixture-container; return 0 ;;
    logs) echo fixture-mongo-log; return 0 ;;
    exec)
      local probe_home=/data/db previous= argument
      for argument in "$@"; do
        if [[ "$previous" == --env || "$previous" == -e ]] && [[ "$argument" == HOME=* ]]; then probe_home="\${argument#HOME=}"; fi
        previous="$argument"
      done
      # The old invocation inherits the image's /data/db home. Shell log
      # lock activity there conflicts with the entrypoint's ownership walk.
      case "$probe_home" in /data/db|/data/db/*|/data/configdb|/data/configdb/*) return 1 ;; esac
      local attempt; attempt=$(grep -c '^exec' "$PROBE_TRACE")
      if [[ "$PROBE_MODE" == never ]]; then return 1; fi
      if [[ "$PROBE_MODE" == zero ]]; then echo 0; return 0; fi
      if [[ "$PROBE_MODE" == delayed && "$attempt" -lt 3 ]]; then return 1; fi
      echo 1; return 0 ;;
    *) return 2 ;;
  esac
}
sleep() { printf 'sleep\\t%s\\n' "$*" >> "$PROBE_TRACE"; }
`;
function run(label,body,mode){
 const trace=path.join(work,label+'.trace'),file=path.join(work,label+'.sh');
 fs.writeFileSync(file,fake+'\n'+body+'\n');
 const result=cp.spawnSync(bash,['--noprofile','--norc','-e',file.replace(/\\/g,'/')],{
  encoding:'utf8',timeout:20000,env:{...process.env,BASH_ENV:'',PROBE_TRACE:trace.replace(/\\/g,'/'),PROBE_MODE:mode}});
 if(result.error)throw result.error;
 return {...result,calls:fs.existsSync(trace)?fs.readFileSync(trace,'utf8').trim().split('\n').map(line=>line.split('\t')):[]};
}
try{
 const immediate=run('immediate',ready.script,'ready'),later=run('delayed',ready.script,'delayed');
 const never=run('never',ready.script,'never'),zero=run('zero',ready.script,'zero');
 const probes=result=>result.calls.filter(call=>call[0]==='exec');
 yes('a ready workflow ping invocation succeeds on the first probe',immediate.status===0&&probes(immediate).length===1);
 yes('a temporarily unavailable database succeeds only after its third workflow ping',later.status===0&&probes(later).length===3);
 const all=[...probes(immediate),...probes(later),...probes(never),...probes(zero)];
 yes('every shell probe has an explicit HOME outside both database trees',all.length>0&&all.every(call=>{
  const at=call.findIndex(arg=>arg==='--env'||arg==='-e'),value=at>=0?call[at+1]:'';
  if(!value||!value.startsWith('HOME=/'))return false;
  const home=path.posix.normalize(value.slice(5));return !['/data/db','/data/configdb'].some(dir=>home===dir||home.startsWith(dir+'/'));
 }));
 yes('every probe disables startup configuration and executes the original Mongo ping',all.length>0&&all.every(call=>
  call.includes('--norc')&&call.includes('--quiet')&&call.includes('db.runCommand({ping:1}).ok')&&call.includes('test-mongo')));
 for(const[name,result]of[['unavailable',never],['zero ping result',zero]]){
  yes(name+' still fails after exactly sixty unchanged waits and emits database logs',result.status!==0&&probes(result).length===60&&
   result.calls.filter(call=>call[0]==='sleep'&&call[1]==='1').length===60&&
   result.calls.filter(call=>call[0]==='logs'&&call[1]==='test-mongo').length===1&&/mongod did not answer in 60 s/.test(result.stdout));
 }
 const start=immediate.calls.find(call=>call[0]==='run')||[];
 yes('the database retains its immutable image, loopback bind and ordinary container HOME',
  start.some(arg=>/^mongo:8\.0\.38@sha256:[a-f0-9]{64}$/.test(arg))&&
  start.includes('--bind_ip')&&start.includes('127.0.0.1')&&start.includes('27017')&&!start.some(arg=>arg.startsWith('HOME=')));
 const tests=run('ctest',test.script,'ready'),args=tests.calls.find(call=>call[0]==='run')||[];
 yes('the actual Test step still passes the real Mongo URI and full CTest flags',tests.status===0&&
  args.includes('SKYMP_TEST_MONGO_URI=mongodb://127.0.0.1:27017')&&args.includes('--network')&&args.includes('host')&&
  JSON.stringify(args.slice(args.indexOf('ctest')))===JSON.stringify(['ctest','-C','RelWithDebInfo','--verbose','--output-on-failure']));
 yes('readiness and CTest keep the same existing skip-tests condition',ready.condition==='if: ${{ !inputs.skip_tests }}'&&test.condition===ready.condition);
 const job=fast.slice(fast.indexOf('  client-view-tests:'),fast.indexOf('  save-directory-linux:'));
 yes('the actual shell regression runs in the existing fast client-view CI job',job.includes('node misc/tests/node/test_mongo_readiness_probe.js'));
}finally{
 const parent=fs.realpathSync(os.tmpdir()),target=fs.realpathSync(work);
 if(path.dirname(target).toLowerCase()!==parent.toLowerCase()||!path.basename(target).startsWith('thornswood-mongo-probe-'))throw Error('Unsafe fixture cleanup target');
 fs.rmSync(target,{recursive:true,force:true});
}
console.log(pass+' passed, '+fail+' failed');process.exitCode=fail?1:0;
