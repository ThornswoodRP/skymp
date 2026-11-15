import * as fs from "fs";
import * as path from "path";
import * as os from "os";

import { ClientScriptsRecorder } from "./gamemodeClientScripts";

// Loading the gamemode, at the start and at every change of the file. Out of
// index.ts so it can be tested on its own (Thornswood #3196).

// The gamemode.js each path last loaded: its text, and which file on disk it
// was (Thornswood #6149). chokidar tells about one arrival of the file more
// than once (a rename is an unlink and an add, then a change), and only the
// first of those may load. A file put in place again is a new arrival even
// when its text is the same: the gamemode reads its tables at load, and a
// deploy that changes only the tables puts the same gamemode.js back in so it
// loads them. Holding a load to the text alone skipped exactly that load, so
// the deploy waited for a load that never came.
type Loaded = { contents: string; ino: number; mtimeMs: number };
const gamemodeCache = new Map<string, Loaded>();

// Its text and which file it is, from one open of it, so the two belong to
// the same file even while a deploy replaces it.
function readGamemode(file: string): Loaded {
  const fd = fs.openSync(file, "r");
  try {
    const st = fs.fstatSync(fd);
    return { contents: fs.readFileSync(fd, "utf8"), ino: st.ino, mtimeMs: st.mtimeMs };
  } finally {
    fs.closeSync(fd);
  }
}

function sameArrival(a: Loaded | undefined, b: Loaded): boolean {
  return !!a && a.contents === b.contents && a.ino === b.ino && a.mtimeMs === b.mtimeMs;
}

// True once the gamemode's top level ran to the end, false when it threw.
// The text is the one read for the cache, so what loads is what was compared.
function requireTemp(contents: string): boolean {
  // https://blog.mastykarz.nl/create-temp-directory-app-node-js/
  let tmpDir;
  const appPrefix = 'skymp5-server';
  try {
    tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), appPrefix));

    const tempPath = path.join(tmpDir, Math.random() + '-' + Date.now() + '.js');
    fs.writeFileSync(tempPath, contents);

    require(tempPath);
    return true;
  } catch (e) {
    console.error(e.stack);
    return false;
  } finally {
    try {
      if (tmpDir) {
        fs.rmSync(tmpDir, { recursive: true });
      }
    } catch (e) {
      console.error(`An error has occurred while removing the temp folder at ${tmpDir}. Please remove it manually. Error: ${e}`);
    }
  }
}

export function requireUncached(
  module: string,
  recorder: ClientScriptsRecorder,
  server: any
): void {
  const now = readGamemode(require.resolve(module));

  // Load gamemode.js once for each time it is put in place, not again for
  // the other events chokidar sends about the same arrival.
  if (!sameArrival(gamemodeCache.get(module), now)) {
    gamemodeCache.set(module, now);

    while (1) {
      try {
        recorder.clear();

        // In native module we now register mp-api methods into the ScampServer class
        // This workaround allows code that is bound to global 'mp' object to run
        // @ts-ignore
        globalThis.mp = globalThis.mp || server;
        // Each load sets its own; one an earlier load left must not answer
        // for a gamemode that no longer has it.
        delete (globalThis as { __thornswoodGamemodeLoaded?: unknown }).__thornswoodGamemodeLoaded;

        if (!requireTemp(now.contents)) {
          // Thornswood #3196: a deploy that reloads the gamemode waits for
          // this line or the next one to know how the load went.
          console.log("[thornswood] gamemode did not load; the error is above");
          return;
        }
        const load = recorder.loaded();
        console.log(`[thornswood] gamemode client scripts ${load.digest} (${load.scripts}), ` +
          (!load.reload ? "the first load in this process" :
            load.clientScriptsChanged ? "changed since the last load" : "unchanged since the last load"));
        const loaded = (globalThis as { __thornswoodGamemodeLoaded?: (load: object) => void }).__thornswoodGamemodeLoaded;
        if (typeof loaded === "function") {
          try {
            loaded({ reload: load.reload, clientScriptsChanged: load.clientScriptsChanged });
          } catch (e) {
            console.error(`in the gamemode's __thornswoodGamemodeLoaded:\n${e.stack}`);
          }
        }
        return;
      } catch (e) {
        if (`${e}`.indexOf("'JsRun' returned error 0x30002") === -1) {
          throw e;
        } else {
          console.log("Bad syntax, ignoring");
          return;
        }
      }
    }
  }
}
