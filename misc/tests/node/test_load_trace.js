'use strict';
// Thornswood #6200: the start is measured up to the game's data load. On
// 26 Nov 2026 the page drew nine seconds before the data load, and no cursor
// can come up before it. These checks read the source, since the trace runs
// inside the game: that it starts on the main thread at the plugin's load,
// marks every SKSE message, stops at the data load, and never takes a lock
// or writes a line while the main thread is stopped for a look.
const fs = require('node:fs'), path = require('node:path');
const dir = path.join(__dirname, '../../../skyrim-platform/src/platform_se/skyrim_platform');
const src = fs.readFileSync(path.join(dir, 'LoadTrace.cpp'), 'utf8');
const main = fs.readFileSync(path.join(dir, 'main.cpp'), 'utf8');
let failed = 0;
function check(name, ok) {
  console.log((ok ? 'PASS ' : 'FAIL ') + name);
  if (!ok) failed++;
}
function body(text, signature) {
  const at = text.indexOf(signature);
  if (at < 0) return '';
  const open = text.indexOf('{', at);
  let depth = 0;
  for (let i = open; i < text.length; i++) {
    if (text[i] === '{') depth++;
    else if (text[i] === '}' && --depth === 0) return text.slice(open, i + 1);
  }
  return '';
}
const code = (s) => s.replace(/\/\/[^\n]*/g, '').replace(/\/\*[\s\S]*?\*\//g, '');

const load = code(body(main, 'SKSEPlugin_Load_Impl(const SKSE::LoadInterface* skse)'));
check('the trace starts in the plugin\'s load, after SKSE is set up',
      load.indexOf('LoadTrace::Start();') > load.indexOf('SKSE::Init(skse);') &&
      load.indexOf('SKSE::Init(skse);') >= 0);
check('  and sees every SKSE message before the handlers run',
      /RegisterListener\(\s*\[\]\(SKSE::MessagingInterface::Message\* a_msg\) \{\s*LoadTrace::Mark\(a_msg->type\);/.test(load));

const look = code(body(src, 'bool MainThreadAt(uint64_t& rip)'));
const suspend = look.indexOf('SuspendThread(g_mainThread)');
const resume = look.indexOf('ResumeThread(g_mainThread)');
const between = look.slice(suspend, resume);
check('a look stops the main thread and lets it go again', suspend >= 0 && resume > suspend);
check('  with only the context read while it is stopped',
      /GetThreadContext\(g_mainThread, &context\)/.test(between) &&
      !/lock|logger|ModuleOf|GetModuleHandle|new |std::string|push_back/.test(between));

const sample = code(body(src, 'void Sample()'));
check('the module is named after the main thread runs again',
      sample.indexOf('MainThreadAt(rip)') >= 0 &&
      sample.indexOf('ModuleOf(rip, handle)') > sample.indexOf('MainThreadAt(rip)'));
check('the sampler runs until the data load, then writes the totals',
      /while \(!g_dataLoaded\)/.test(sample) && sample.indexOf('Report(ended') > sample.indexOf('while (!g_dataLoaded)'));

const mark = code(body(src, 'void LoadTrace::Mark(uint32_t skseMessageType)'));
check('the data load\'s message ends it', /if \(skseMessageType == kDataLoaded\) \{\s*g_dataLoaded = true;/.test(mark) &&
      /constexpr uint32_t kDataLoaded = 8;/.test(src));
check('  and nothing is marked after it', /if \(!g_mainThread \|\| g_dataLoaded\)/.test(mark));
check('each line it writes starts with [load]',
      (src.match(/logger::info\("([^"]*)"/g) || []).every((l) => l.startsWith('logger::info("[load]')) &&
      (src.match(/logger::info\(/g) || []).length === (src.match(/logger::info\("\[load\]/g) || []).length +
        (src.match(/logger::info\(\s*\n\s*"\[load\]/g) || []).length);

if (failed) { console.log(failed + ' check(s) failed'); process.exitCode = 1; }
