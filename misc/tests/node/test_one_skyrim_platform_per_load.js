'use strict';
// Thornswood #6200: the skyrimPlatform exports are built once per load, not
// once per plugin. On 26 Nov 2026 building them took about 200 ms for every
// plugin, the nine plugins held the game's main thread for 2750 ms right
// after the data load, and the game read no mouse in that time: the cursor
// took a while to show, and the motion made then came in one read. These
// checks read the source, since the plugins run inside the game.
const fs = require('node:fs'), path = require('node:path');
const src = fs.readFileSync(path.join(__dirname,
  '../../../skyrim-platform/src/platform_se/skyrim_platform/SkyrimPlatform.cpp'), 'utf8');
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

const load = code(body(src, 'void LoadPluginFile(Napi::Env env, const std::filesystem::path& path)'));
const build = load.indexOf("skyrimPlatform = addNativeExports('skyrimPlatform', {})");
const guard = load.search(/if \(!exportsThisLoad\)/);
check('a plugin builds the exports only when this load has none yet', build >= 0 && guard >= 0 && guard < build);
check('  keeps the ones it built for the rest of the load',
      /exportsThisLoad\.reset\(\s*new Napi::Reference<Napi::Object>\(Napi::Persistent\(exports\)\)\)/.test(load) &&
      /env\.Global\(\)\.Get\("skyrimPlatform"\)/.test(load));
check('  and the plugins after it are handed those',
      /else \{\s*env\.Global\(\)\.Set\("skyrimPlatform", exportsThisLoad->Value\(\)\);\s*\}/.test(load));
check('  before the plugin\'s own script runs',
      load.indexOf('exportsThisLoad->Value()') >= 0 &&
      load.indexOf('exportsThisLoad->Value()') < load.indexOf('engine->RunScript(env, scriptSrc'));
check('the exports are built in one place only', (code(src).match(/addNativeExports\('skyrimPlatform'/g) || []).length === 1);

const tick = code(body(src, 'void TickImpl(Napi::Env env)'));
const reset = tick.indexOf('exportsThisLoad.reset();');
check('every load, at startup and on a reload, builds them again',
      reset >= 0 && reset > tick.indexOf('if (loadNeeded)') && reset < tick.indexOf('LoadFiles('));
check('the listener keeps them', /std::unique_ptr<Napi::Reference<Napi::Object>> exportsThisLoad;/.test(src));

if (failed) { console.log(failed + ' check(s) failed'); process.exitCode = 1; }
