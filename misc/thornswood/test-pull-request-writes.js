// Fails when a job a pull request runs could write a package or the vcpkg
// library cache (Thornswood #2760).
//
// WHY. The push build of thornswood/engine-changes is the engine Thornswood
// ships, and it restores vcpkg's libraries from GitHub Packages. A job a pull
// request runs, holding packages: write or a vcpkg binary source that writes,
// could put a library there from code nobody reviewed, and the next shipped
// build would link it. thornswood-pr-windows.yml keeps the two apart today
// (build-pr reads, build writes, and build runs on a push only), and nothing
// else stopped a later edit from handing a pull request's job write access
// again. This does.
//
// WHAT FAILS. For every workflow in .github/workflows that a pull_request or
// pull_request_target event starts, every job that event can run:
//   - holds packages: write, or write-all, from its own permissions, from the
//     workflow's, or from the job that calls it when it is a reusable
//     workflow's job;
//   - takes the repository's default token, with no permissions block in the
//     job, the workflow or the caller: the default is a setting this check
//     cannot read, so a pull request's job must say what it holds;
//   - sees a VCPKG_BINARY_SOURCES that writes (a source ending in write or
//     readwrite), from the workflow's env, its own env or a step's env, or
//     one picked by an expression, which this check cannot read;
//   - has a run step that sets VCPKG_BINARY_SOURCES to a source that writes;
//   - runs a local action (./.github/actions/...) with any of those in its
//     steps, or calls a local reusable workflow with any of those in its jobs;
//   - calls a reusable workflow from outside this repository, which this check
//     cannot read.
// A file that cannot be read as the YAML a workflow is written in fails too,
// so a reader that stopped understanding a file cannot pass by finding nothing.
//
// WHICH JOBS A PULL REQUEST RUNS. All of them, except a job whose `if` can only
// be true for another event (github.event_name == 'push', or a chain of such
// tests joined by ||) or only in upstream's repository (github.repository ==
// 'skyrim-multiplayer/skymp', the guard upstream's workflows carry on this
// branch). Any other `if` is read as one a pull request can meet. Branch and
// path filters are not read: a pull request into another branch runs this
// branch's copy of a workflow whenever its head carries this branch's files.
//
// Then the same rules are driven over made up workflows in a temp folder, one
// for each way a job can come to write, so a rule that stopped matching shows.
//
// Node 20 or newer, nothing installed, read only apart from its own temp
// folder. Exit 0 when every check passes, 1 when any fails. Run from anywhere:
//     node misc/thornswood/test-pull-request-writes.js

'use strict';

const fs = require('fs');
const os = require('os');
const path = require('path');

// ---- a reader for the YAML workflows are written in -------------------------
// Block mappings and sequences, flow [ ] and { }, quoted and plain scalars,
// | and > blocks, anchors, aliases and << merges: everything GitHub's own
// workflow files use. Anything else is an error, never a guess.

class YamlError extends Error {}

function stripComment(s) {
  let q = null;
  for (let i = 0; i < s.length; i++) {
    const c = s[i];
    if (q) {
      if (q === "'" && c === "'") {
        if (s[i + 1] === "'") { i++; continue; }
        q = null;
      } else if (q === '"' && c === '\\') {
        i++;
      } else if (q === '"' && c === '"') {
        q = null;
      }
      continue;
    }
    if ((c === "'" || c === '"') && (i === 0 || /[\s[{,:]/.test(s[i - 1]))) { q = c; continue; }
    if (c === '#' && (i === 0 || /\s/.test(s[i - 1]))) return s.slice(0, i).replace(/\s+$/, '');
  }
  return s.replace(/\s+$/, '');
}

// The index just after a quoted scalar that starts at s[0], or -1.
function quotedEnd(s) {
  const q = s[0];
  for (let i = 1; i < s.length; i++) {
    if (q === '"' && s[i] === '\\') { i++; continue; }
    if (s[i] === q) {
      if (q === "'" && s[i + 1] === "'") { i++; continue; }
      return i + 1;
    }
  }
  return -1;
}

function unquote(s) {
  if (s[0] === "'") return s.slice(1, -1).replace(/''/g, "'");
  return s.slice(1, -1).replace(/\\(u[0-9a-fA-F]{4}|x[0-9a-fA-F]{2}|.)/g, (m, e) => {
    if (e[0] === 'u' || e[0] === 'x') return String.fromCharCode(parseInt(e.slice(1), 16));
    return { n: '\n', t: '\t', r: '\r', '0': '\0', ' ': ' ', '/': '/', '\\': '\\', '"': '"' }[e] ?? e;
  });
}

function isSeqItem(t) { return t === '-' || t.startsWith('- '); }

// { key, rest } when the line is `key: rest`, else null.
function splitKey(t) {
  if (t[0] === '"' || t[0] === "'") {
    const end = quotedEnd(t);
    if (end < 0) return null;
    const m = /^\s*:(?:\s+|$)/.exec(t.slice(end));
    if (!m) return null;
    return { key: unquote(t.slice(0, end)), rest: t.slice(end + m[0].length).trim() };
  }
  if (/^[[\]{}&*!|>%@`?,]/.test(t) || isSeqItem(t)) return null;
  for (let k = 0; k < t.length; k++) {
    if (t[k] === ':' && (k + 1 === t.length || /\s/.test(t[k + 1]))) {
      const key = t.slice(0, k).trim();
      if (!key) return null;
      return { key, rest: t.slice(k + 1).trim() };
    }
  }
  return null;
}

class Reader {
  constructor(text, name) {
    this.raw = text.replace(/^\uFEFF/, '').split(/\r?\n/);
    this.name = name;
    this.i = 0;
    this.virt = null;
    this.anchors = Object.create(null);
  }

  fail(msg, line) { throw new YamlError(`${this.name}:${(line ?? this.i) + 1}: ${msg}`); }

  // The next line that holds something, as { indent, text }, without taking it.
  peek() {
    if (this.virt && this.virt.i === this.i) return { indent: this.virt.indent, text: this.virt.text };
    while (this.i < this.raw.length) {
      const m = /^( *)(.*)$/.exec(this.raw[this.i]);
      const text = stripComment(m[2]);
      if (text === '' || (m[1].length === 0 && text === '---')) { this.i++; continue; }
      if (text[0] === '\t') this.fail('a tab in the indentation');
      return { indent: m[1].length, text };
    }
    return null;
  }

  take() { this.i++; }

  document() {
    const n = this.peek();
    if (!n) return null;
    const v = this.block(n.indent, -1);
    const after = this.peek();
    if (after) this.fail(`more than one top level node (indent ${after.indent})`);
    return v;
  }

  // A node starting on the next line, more indented than parent.
  block(indent, parent) {
    const n = this.peek();
    if (isSeqItem(n.text)) return this.seq(indent);
    if (splitKey(n.text)) return this.map(indent);
    const line = this.i;
    this.take();
    return this.scalar(n.text, parent, line);
  }

  map(indent) {
    const out = Object.create(null);
    for (;;) {
      const n = this.peek();
      if (!n || n.indent < indent) break;
      if (n.indent > indent) this.fail(`indented more than the mapping it is in (${n.indent}, not ${indent})`);
      if (isSeqItem(n.text)) break;
      if (n.text.startsWith('? ')) this.fail('a complex key');
      const kv = splitKey(n.text);
      if (!kv) this.fail(`expected a key, found "${n.text}"`);
      const line = this.i;
      this.take();
      const v = this.value(kv.rest, indent, line, true);
      if (kv.key === '<<') {
        for (const src of Array.isArray(v) ? v : [v]) {
          if (!isObj(src)) this.fail('a << merge of something that is not a mapping', line);
          for (const k of Object.keys(src)) if (!(k in out)) out[k] = src[k];
        }
        continue;
      }
      if (Object.prototype.hasOwnProperty.call(out, kv.key)) this.fail(`the key "${kv.key}" twice in one mapping`, line);
      out[kv.key] = v;
    }
    return out;
  }

  seq(indent) {
    const out = [];
    for (;;) {
      const n = this.peek();
      if (!n || n.indent < indent) break;
      if (n.indent > indent) this.fail(`indented more than the list it is in (${n.indent}, not ${indent})`);
      if (!isSeqItem(n.text)) break;
      const line = this.i;
      const after = n.text === '-' ? '' : n.text.slice(2);
      const rest = after.trimStart();
      const at = indent + 2 + (after.length - rest.length);
      if (rest !== '' && (isSeqItem(rest) || splitKey(rest))) {
        this.virt = { i: this.i, indent: at, text: rest };
        out.push(isSeqItem(rest) ? this.seq(at) : this.map(at));
      } else {
        this.take();
        out.push(this.value(rest, indent, line, false));
      }
    }
    return out;
  }

  // What follows `key:` or `- ` on its line; parent is the key's or dash's indent.
  value(rest, parent, line, inMap) {
    let anchor = null;
    let m;
    while ((m = /^([&!]\S*)(?:\s+|$)/.exec(rest))) {
      if (m[1][0] === '&') anchor = m[1].slice(1);
      rest = rest.slice(m[0].length);
    }
    let v;
    if (rest === '') {
      const n = this.peek();
      if (n && n.indent > parent) v = this.block(n.indent, parent);
      else if (n && inMap && n.indent === parent && isSeqItem(n.text)) v = this.seq(parent);
      else v = null;
    } else if (rest[0] === '*') {
      const name = rest.slice(1).trim();
      if (!(name in this.anchors)) this.fail(`the alias *${name} names no anchor before it`, line);
      v = this.anchors[name];
    } else if (rest[0] === '|' || rest[0] === '>') {
      if (!/^[|>][-+1-9]{0,2}$/.test(rest)) this.fail(`a block scalar header "${rest}"`, line);
      v = this.blockScalar(parent, rest[0] === '|');
    } else {
      v = this.scalar(rest, parent, line);
    }
    if (anchor) this.anchors[anchor] = v;
    return v;
  }

  // A scalar or flow node whose first line is text; later lines more indented
  // than parent continue it.
  scalar(text, parent, line) {
    const more = () => {
      const n = this.peek();
      if (!n || n.indent <= parent) return null;
      this.take();
      return n.text;
    };
    if (text[0] === '[' || text[0] === '{') {
      let s = text;
      while (!flowClosed(s)) {
        const next = more();
        if (next === null) this.fail('a [ or { that is never closed', line);
        s += ' ' + next;
      }
      return this.flow(s, line);
    }
    if (text[0] === '"' || text[0] === "'") {
      let s = text;
      let end = quotedEnd(s);
      while (end < 0) {
        if (this.i >= this.raw.length) this.fail('a quote that is never closed', line);
        s += ' ' + this.raw[this.i].trim();
        this.take();
        end = quotedEnd(s);
      }
      if (stripComment(s.slice(end)).trim() !== '') this.fail(`text after a quoted scalar: "${s.slice(end)}"`, line);
      return unquote(s.slice(0, end));
    }
    let s = text;
    for (let next = more(); next !== null; next = more()) s += ' ' + next;
    return s;
  }

  blockScalar(parent, literal) {
    const lines = [];
    while (this.i < this.raw.length) {
      const r = this.raw[this.i];
      if (r.trim() !== '' && /^ */.exec(r)[0].length <= parent) break;
      lines.push(r);
      this.i++;
    }
    while (lines.length && lines[lines.length - 1].trim() === '') lines.pop();
    const first = lines.find((l) => l.trim() !== '');
    const cut = first ? /^ */.exec(first)[0].length : 0;
    const body = lines.map((l) => l.slice(Math.min(cut, /^ */.exec(l)[0].length)));
    return literal ? body.join('\n') + '\n' : body.join(' ') + '\n';
  }

  flow(s, line) {
    let i = 0;
    const ws = () => { while (i < s.length && /\s/.test(s[i])) i++; };
    const node = () => {
      ws();
      if (s[i] === '[') {
        i++;
        const out = [];
        for (;;) {
          ws();
          if (s[i] === ']') { i++; return out; }
          out.push(node());
          ws();
          if (s[i] === ',') { i++; continue; }
          if (s[i] === ']') { i++; return out; }
          this.fail(`a flow list that is not [a, b] at "${s.slice(i)}"`, line);
        }
      }
      if (s[i] === '{') {
        i++;
        const out = Object.create(null);
        for (;;) {
          ws();
          if (s[i] === '}') { i++; return out; }
          const k = node();
          ws();
          if (s[i] !== ':') this.fail(`a flow mapping entry with no colon at "${s.slice(i)}"`, line);
          i++;
          out[String(k)] = node();
          ws();
          if (s[i] === ',') { i++; continue; }
          if (s[i] === '}') { i++; return out; }
          this.fail(`a flow mapping that is not {a: b} at "${s.slice(i)}"`, line);
        }
      }
      if (s[i] === '"' || s[i] === "'") {
        const end = quotedEnd(s.slice(i));
        if (end < 0) this.fail('a quote that is never closed', line);
        const v = unquote(s.slice(i, i + end));
        i += end;
        return v;
      }
      const start = i;
      while (i < s.length && !/[,\]}]/.test(s[i]) && !(s[i] === ':' && /[\s,\]}]/.test(s[i + 1] ?? ' '))) i++;
      return s.slice(start, i).trim();
    };
    const v = node();
    ws();
    if (i !== s.length) this.fail(`text after a flow node: "${s.slice(i)}"`, line);
    return v;
  }
}

function flowClosed(s) {
  let depth = 0;
  for (let i = 0; i < s.length; i++) {
    const c = s[i];
    if (c === '"' || c === "'") {
      const end = quotedEnd(s.slice(i));
      if (end < 0) return false;
      i += end - 1;
      continue;
    }
    if (c === '[' || c === '{') depth++;
    if (c === ']' || c === '}') depth--;
  }
  return depth <= 0;
}

function readYaml(text, name) { return new Reader(text, name).document(); }

function isObj(v) { return v !== null && typeof v === 'object' && !Array.isArray(v); }

// ---- the rule ---------------------------------------------------------------

const PR_EVENTS = ['pull_request', 'pull_request_target'];
const UPSTREAM = 'skyrim-multiplayer/skymp';
const SOURCES = 'VCPKG_BINARY_SOURCES';

function triggers(on) {
  if (on === undefined || on === null) return [];
  if (typeof on === 'string') return [on];
  if (Array.isArray(on)) return on.map(String);
  if (isObj(on)) return Object.keys(on);
  return [];
}

// Whether a job whose `if` is cond can run on one of the events in evs.
function canRun(cond, evs) {
  if (cond === undefined || cond === null) return true;
  let s = String(cond).trim();
  const m = /^\$\{\{([\s\S]*)\}\}$/.exec(s);
  if (m) s = m[1].trim();
  if (s === 'false') return false;
  if (/[()]/.test(s) || /!(?!=)/.test(s)) return true;
  return s.split('||').some((d) => d.split('&&').every((c) => !neverHere(c.trim(), evs)));
}

function neverHere(c, evs) {
  let m = /^github\.event_name\s*==\s*(['"])([A-Za-z_]+)\1$/.exec(c) || /^(['"])([A-Za-z_]+)\1\s*==\s*github\.event_name$/.exec(c);
  if (m) return !evs.includes(m[2]);
  m = /^github\.repository\s*==\s*(['"])([^'"]+)\1$/.exec(c) || /^(['"])([^'"]+)\1\s*==\s*github\.repository$/.exec(c);
  if (m) return m[2].toLowerCase() === UPSTREAM;
  return false;
}

// packages access a permissions value gives: 'write', 'read' or 'none'.
// Throws a fault string for a value GitHub would not take or this cannot read.
function packagesOf(p) {
  if (p === null) throw 'an empty permissions block';
  if (typeof p === 'string') {
    if (p === 'write-all') return 'write';
    if (p === 'read-all') return 'read';
    throw `permissions "${p}", which is neither a mapping, read-all nor write-all`;
  }
  if (!isObj(p)) throw 'a permissions value that is not a mapping';
  const v = p.packages;
  if (v === undefined || v === 'none') return 'none';
  if (v === 'read' || v === 'write') return v;
  throw `packages: ${v}`;
}

// Why a VCPKG_BINARY_SOURCES value writes, or null when it only reads.
function sourceWrites(v) {
  if (v === undefined || v === null) return null;
  const s = String(v);
  if (s.includes('${{')) return `is picked by an expression (${s}), which this check cannot read`;
  for (const seg of s.split(';')) {
    const f = seg.split(',').map((x) => x.trim());
    const last = f[f.length - 1].toLowerCase();
    if (f.length > 1 && (last === 'write' || last === 'readwrite')) return `is "${s}", whose source "${seg.trim()}" writes`;
  }
  return null;
}

function runWrites(run) {
  return typeof run === 'string' && run.includes(SOURCES) && /,\s*(?:read)?write\b/i.test(run);
}

function analyse(root) {
  const faults = [];
  const reached = [];
  const skipped = [];
  const cache = new Map();
  const rel = (p) => path.relative(root, p).split(path.sep).join('/');

  const load = (file) => {
    if (cache.has(file)) return cache.get(file);
    let doc = null;
    try {
      doc = readYaml(fs.readFileSync(file, 'utf8'), rel(file));
      if (!isObj(doc)) throw new YamlError(`${rel(file)}: not a mapping at the top`);
    } catch (e) {
      faults.push(e instanceof YamlError ? `${e.message} (it cannot be read, so nothing in it can be checked)` : `${rel(file)}: ${e.message}`);
      doc = null;
    }
    cache.set(file, doc);
    return doc;
  };

  const flows = path.join(root, '.github', 'workflows');
  const files = fs.existsSync(flows)
    ? fs.readdirSync(flows).filter((f) => /\.ya?ml$/.test(f)).sort().map((f) => path.join(flows, f))
    : [];

  // A local action's steps: their env and run.
  const checkAction = (ref, where, depth, seen) => {
    const dir = path.join(root, ref.replace(/^\.\//, ''));
    const file = ['action.yml', 'action.yaml'].map((f) => path.join(dir, f)).find((f) => fs.existsSync(f));
    if (!file) { faults.push(`${where} runs ${ref}, which has no action.yml this check can read`); return; }
    if (seen.includes(file) || depth > 5) { faults.push(`${where} runs ${ref} inside itself`); return; }
    const doc = load(file);
    if (!doc) return;
    const steps = isObj(doc.runs) && Array.isArray(doc.runs.steps) ? doc.runs.steps : [];
    checkSteps(steps, `${where} > ${rel(file)}`, depth + 1, [...seen, file]);
  };

  // A job's or an action's steps: each one's own env, its run, and the
  // local action it runs.
  const checkSteps = (steps, where, depth, seen) => {
    steps.forEach((st, n) => {
      if (!isObj(st)) return;
      const at = `${where} step ${n + 1}${st.name ? ` (${st.name})` : ''}`;
      if (st.env !== undefined && !isObj(st.env)) faults.push(`${at} has an env that is not a mapping, which this check cannot read`);
      if (isObj(st.env) && SOURCES in st.env) {
        const w = sourceWrites(st.env[SOURCES]);
        if (w) faults.push(`${at}: ${SOURCES} in its env ${w}`);
      }
      if (runWrites(st.run)) faults.push(`${at} sets ${SOURCES} to a source that writes in its run`);
      if (typeof st.uses === 'string' && st.uses.startsWith('./')) checkAction(st.uses, at, depth, seen);
    });
  };

  // One job a pull request can run. caller: { p, from } when it is a reusable
  // workflow's job, the packages access its caller holds.
  const checkJob = (file, wf, id, job, evs, caller, trail, depth, seen) => {
    const where = trail ? `${trail} > ${rel(file)} ${id}` : `${rel(file)} ${id}`;
    if (!isObj(job)) { faults.push(`${where} is not a mapping`); return; }
    reached.push(where);

    let p = null;
    let from = null;
    try {
      if ('permissions' in job) { p = packagesOf(job.permissions); from = 'its own permissions'; }
      else if ('permissions' in wf) { p = packagesOf(wf.permissions); from = `the permissions of ${rel(file)}`; }
      else if (caller) { p = caller.p; from = caller.from; }
    } catch (e) {
      faults.push(`${where} has ${e}`);
      p = 'unknown';
    }
    if (p === null) faults.push(`${where} takes the repository's default token: give the job or its workflow a permissions block, since the default is a setting this check cannot read`);
    if (p === 'write') faults.push(`${where} holds packages: write, from ${from}`);

    if (typeof job.uses === 'string') {
      const ref = job.uses.trim();
      if (!ref.startsWith('./')) { faults.push(`${where} calls ${ref}, a workflow outside this repository that this check cannot read`); return; }
      const target = path.join(root, ref.replace(/^\.\//, '').replace(/@.*$/, ''));
      if (!fs.existsSync(target)) { faults.push(`${where} calls ${ref}, which is not in this repository`); return; }
      if (seen.includes(target) || depth > 5) { faults.push(`${where} calls ${ref} inside itself`); return; }
      const called = load(target);
      if (!called) return;
      if (!isObj(called.jobs)) { faults.push(`${rel(target)} has no jobs`); return; }
      const pass = p === null || p === 'unknown' ? { p: 'unknown', from: `the caller ${where}` } : { p, from: from === 'its own permissions' ? `the permissions of ${where}` : from };
      for (const [cid, cjob] of Object.entries(called.jobs)) {
        if (isObj(cjob) && !canRun(cjob.if, evs)) { skipped.push(`${where} > ${rel(target)} ${cid}`); continue; }
        checkJob(target, called, cid, cjob, evs, pass, where, depth + 1, [...seen, target]);
      }
      return;
    }

    if (wf.env !== undefined && !isObj(wf.env)) faults.push(`${rel(file)} has an env that is not a mapping, which this check cannot read`);
    if (job.env !== undefined && !isObj(job.env)) faults.push(`${where} has an env that is not a mapping, which this check cannot read`);
    const jobEnv = isObj(job.env) && SOURCES in job.env;
    const wfEnv = isObj(wf.env) && SOURCES in wf.env;
    if (jobEnv || wfEnv) {
      const w = sourceWrites(jobEnv ? job.env[SOURCES] : wf.env[SOURCES]);
      if (w) faults.push(`${where}: ${SOURCES} ${w}, from ${jobEnv ? 'its own env' : `the env of ${rel(file)}`}`);
    }
    checkSteps(Array.isArray(job.steps) ? job.steps : [], where, depth, seen);
  };

  for (const file of files) {
    const wf = load(file);
    if (!wf) continue;
    const evs = triggers(wf.on).filter((e) => PR_EVENTS.includes(e));
    if (!evs.length) continue;
    if (!isObj(wf.jobs)) { faults.push(`${rel(file)} runs on ${evs.join(' and ')} and has no jobs this check can read`); continue; }
    for (const [id, job] of Object.entries(wf.jobs)) {
      if (isObj(job) && !canRun(job.if, evs)) { skipped.push(`${rel(file)} ${id}`); continue; }
      checkJob(file, wf, id, job, evs, null, '', 0, [file]);
    }
  }
  return { files: files.map(rel), faults, reached, skipped };
}

// ---- the checks -------------------------------------------------------------

let pass = 0;
let fail = 0;
function yes(what, cond) {
  if (cond) { pass++; console.log(`PASS  ${what}`); } else { fail++; console.log(`FAIL  ${what}`); }
}

function realFiles(root) {
  const r = analyse(root);
  yes(`.github/workflows is read (${r.files.length} workflows)`, r.files.length >= 10);
  for (const want of ['.github/workflows/thornswood-pr-windows.yml', '.github/workflows/thornswood-pr-linux.yml']) {
    yes(`${want} is read`, r.files.includes(want));
  }
  // The two jobs of the engine build, named, so a rename or a reader that
  // lost the `if` shows here instead of passing on what is left.
  yes('a pull request runs build-pr, the engine build that reads the cache', r.reached.includes('.github/workflows/thornswood-pr-windows.yml build-pr'));
  yes('a pull request does not run build, the push build that writes it', r.skipped.includes('.github/workflows/thornswood-pr-windows.yml build'));
  yes('the Linux build is followed into the reusable workflow it calls',
    r.reached.some((j) => j.endsWith('.github/workflows/linux-build-base.yml linux-build-base')));
  // And the other half of #2760: the push build still fills the cache.
  let build = null;
  try {
    const wf = readYaml(fs.readFileSync(path.join(root, '.github', 'workflows', 'thornswood-pr-windows.yml'), 'utf8'), 'thornswood-pr-windows.yml');
    build = isObj(wf) && isObj(wf.jobs) ? wf.jobs.build : null;
  } catch (e) {
    build = null;
  }
  yes('the push build still writes the library cache (packages: write and a readwrite source)',
    isObj(build) && isObj(build.permissions) && build.permissions.packages === 'write' &&
    isObj(build.env) && sourceWrites(build.env[SOURCES]) !== null && !String(build.env[SOURCES]).includes('${{'));
  yes('no job a pull request runs can write a package or the library cache', r.faults.length === 0);
  for (const f of r.faults) console.log(`      ${f}`);
  console.log('      jobs a pull request runs:');
  for (const j of r.reached) console.log(`        ${j}`);
  console.log('      jobs it does not:');
  for (const j of r.skipped) console.log(`        ${j}`);
}

function plantedFiles() {
  const work = fs.mkdtempSync(path.join(os.tmpdir(), 'fork-pr-writes-'));
  try {
    // One made up repository per case: the workflows given, and any local
    // actions, then the faults analyse finds in it.
    const run = (flows, actions = {}) => {
      const root = fs.mkdtempSync(path.join(work, 'repo-'));
      fs.mkdirSync(path.join(root, '.github', 'workflows'), { recursive: true });
      for (const [name, text] of Object.entries(flows)) fs.writeFileSync(path.join(root, '.github', 'workflows', name), text);
      for (const [dir, text] of Object.entries(actions)) {
        fs.mkdirSync(path.join(root, dir), { recursive: true });
        fs.writeFileSync(path.join(root, dir, 'action.yml'), text);
      }
      return analyse(root);
    };
    const one = (what, r, pattern) => {
      const ok = r.faults.length === 1 && pattern.test(r.faults[0]);
      yes(what, ok);
      if (!ok) for (const f of r.faults) console.log(`      ${f}`);
    };
    const clean = (what, r) => {
      yes(what, r.faults.length === 0);
      for (const f of r.faults) console.log(`      ${f}`);
    };
    const step = '    steps:\n      - run: echo hello\n';

    // The engine build's own shape: two jobs, one for each event, the
    // writing one on push only. Its run text names write in a block scalar,
    // which is text and not permissions.
    clean('the engine build\'s shape is clean: build-pr reads, build writes on push only', run({ 'w.yml': [
      'on:', '  pull_request:', '    branches: [ thornswood/engine-changes ]', '  push:', '  workflow_dispatch:',
      'env:', "  VCPKG_BINARY_SOURCES: 'clear;nuget,GitHub,read'",
      'jobs:',
      '  build-pr:',
      "    if: github.event_name == 'pull_request' || github.event_name == 'workflow_dispatch'",
      '    permissions:', '      contents: read', '      packages: read',
      '    steps: &steps',
      '      - name: say it',
      '        run: |',
      '          echo "permissions:"',
      '          echo "  packages: write"',
      '      - uses: ./.github/actions/base',
      '  build:',
      "    if: github.event_name == 'push'",
      '    permissions:', '      contents: read', '      packages: write',
      '    env:', "      VCPKG_BINARY_SOURCES: 'clear;nuget,GitHub,readwrite'",
      '    steps: *steps', ''].join('\n') },
    { '.github/actions/base': 'runs:\n  using: composite\n  steps:\n    - shell: bash\n      run: echo base\n' }));

    clean('a job guarded to upstream\'s repository is not run here', run({ 'w.yml':
      "on: [push, pull_request]\nenv:\n  VCPKG_BINARY_SOURCES: 'clear;x-gha,readwrite'\njobs:\n  b:\n    if: github.repository == 'skyrim-multiplayer/skymp'  # upstream only\n    permissions: write-all\n" + step }));
    clean('a workflow no pull request starts may write', run({ 'w.yml':
      "on:\n  push:\npermissions:\n  packages: write\nenv:\n  VCPKG_BINARY_SOURCES: 'clear;nuget,GitHub,readwrite'\njobs:\n  b:\n" + step }));
    clean('permissions: {} holds nothing', run({ 'w.yml': 'on: pull_request\npermissions: {}\njobs:\n  b:\n' + step }));

    one('packages: write on a pull request\'s job is caught', run({ 'w.yml':
      'on:\n  pull_request:\njobs:\n  b:\n    permissions:\n      packages: write\n' + step }), /holds packages: write, from its own permissions/);
    one('packages: write the job inherits from its workflow is caught', run({ 'w.yml':
      'on: [push, pull_request]\npermissions:\n  contents: read\n  packages: write\njobs:\n  b:\n' + step }), /holds packages: write, from the permissions of/);
    one('write-all from the workflow, in flow style, is caught', run({ 'w.yml':
      'on: pull_request\npermissions: write-all\njobs:\n  b:\n' + step }), /holds packages: write/);
    one('{ packages: write } in flow style is caught', run({ 'w.yml':
      'on: pull_request\njobs:\n  b:\n    permissions: { contents: read, packages: write }\n' + step }), /holds packages: write/);
    one('a permissions block reached through an alias is caught', run({ 'w.yml':
      'on: pull_request\nx-perms: &w\n  packages: write\njobs:\n  b:\n    permissions: *w\n' + step }), /holds packages: write/);
    one('pull_request_target counts as a pull request', run({ 'w.yml':
      'on:\n  pull_request_target:\n    types: [opened]\njobs:\n  b:\n    permissions:\n      packages: write\n' + step }), /holds packages: write/);
    one('a job that takes the repository\'s default token is caught', run({ 'w.yml':
      'on: pull_request\njobs:\n  b:\n' + step }), /repository's default token/);
    one('an if a pull request can meet does not hide the job', run({ 'w.yml':
      "on: [push, pull_request]\njobs:\n  b:\n    if: github.event_name != 'push'\n    permissions:\n      packages: write\n" + step }), /holds packages: write/);

    one('a readwrite source the job inherits from its workflow\'s env is caught', run({ 'w.yml':
      "on: pull_request\npermissions:\n  contents: read\nenv:\n  VCPKG_BINARY_SOURCES: 'clear;nuget,GitHub,readwrite'\njobs:\n  b:\n" + step }), /writes, from the env of/);
    one('a readwrite source in the job\'s own env is caught', run({ 'w.yml':
      "on: pull_request\npermissions:\n  contents: read\njobs:\n  b:\n    env:\n      VCPKG_BINARY_SOURCES: \"clear;x-gha,readwrite\"\n" + step }), /writes, from its own env/);
    one('a write source in a step\'s env is caught', run({ 'w.yml':
      'on: pull_request\npermissions:\n  contents: read\njobs:\n  b:\n    steps:\n      - name: build\n        env:\n          VCPKG_BINARY_SOURCES: clear;files,/tmp/c,write\n        run: echo x\n' }), /step 1 \(build\).*writes/);
    one('a source picked by an expression is caught', run({ 'w.yml':
      "on: [push, pull_request]\npermissions:\n  contents: read\njobs:\n  b:\n    env:\n      VCPKG_BINARY_SOURCES: ${{ github.event_name == 'push' && 'clear;nuget,GitHub,readwrite' || 'clear;nuget,GitHub,read' }}\n" + step }), /picked by an expression/);
    one('a run step that sets a readwrite source is caught', run({ 'w.yml':
      'on: pull_request\npermissions:\n  contents: read\njobs:\n  b:\n    steps:\n      - run: |\n          echo "VCPKG_BINARY_SOURCES=clear;nuget,GitHub,readwrite" >> "$GITHUB_ENV"\n' }), /sets VCPKG_BINARY_SOURCES to a source that writes/);
    one('a local action with a readwrite source in its steps is caught', run({ 'w.yml':
      'on: pull_request\npermissions:\n  contents: read\njobs:\n  b:\n    steps:\n      - uses: ./.github/actions/base\n' },
    { '.github/actions/base': "runs:\n  using: composite\n  steps:\n    - shell: bash\n      env:\n        VCPKG_BINARY_SOURCES: 'clear;nuget,GitHub,readwrite'\n      run: echo x\n" }), /actions\/base\/action\.yml step 1.*writes/);

    const callee = (body) => ({ 'caller.yml': 'on: pull_request\npermissions:\n  contents: read\njobs:\n  call:\n    uses: ./.github/workflows/base.yml\n', 'base.yml': body });
    one('packages: write at the top of a reusable workflow a pull request calls is caught', run(callee(
      'on:\n  workflow_call:\npermissions:\n  packages: write\njobs:\n  inner:\n' + step)), /caller\.yml call > .*base\.yml inner holds packages: write/);
    one('packages: write on a job of a reusable workflow a pull request calls is caught', run(callee(
      'on: workflow_call\njobs:\n  inner:\n    permissions:\n      packages: write\n' + step)), /base\.yml inner holds packages: write, from its own permissions/);
    const inherits = run({
      'caller.yml': 'on: pull_request\njobs:\n  call:\n    permissions:\n      packages: write\n    uses: ./.github/workflows/base.yml\n',
      'base.yml': 'on: workflow_call\njobs:\n  inner:\n' + step });
    yes('packages: write a reusable workflow\'s job inherits from its caller is caught, in the caller and in the job it calls',
      inherits.faults.length === 2 &&
      /caller\.yml call holds packages: write, from its own permissions/.test(inherits.faults[0]) &&
      /base\.yml inner holds packages: write, from the permissions of .*caller\.yml call/.test(inherits.faults[1]));
    for (const f of inherits.faults) console.log(`      ${f}`);
    one('a readwrite source in a reusable workflow a pull request calls is caught', run(callee(
      "on: workflow_call\nenv:\n  VCPKG_BINARY_SOURCES: 'clear;nuget,GitHub,readwrite'\njobs:\n  inner:\n" + step)), /base\.yml inner: VCPKG_BINARY_SOURCES .*writes/);
    one('a reusable workflow from outside this repository is caught', run({ 'w.yml':
      'on: pull_request\npermissions:\n  contents: read\njobs:\n  call:\n    uses: octo/repo/.github/workflows/build.yml@11d5960a326750d5838078e36cf38b85af677262\n' }), /outside this repository/);
    one('a workflow file that cannot be read is caught', run({ 'w.yml':
      'on: pull_request\njobs:\n  b:\n    permissions:\n      packages: read\n     steps: []\n' }), /cannot be read/);
  } finally {
    fs.rmSync(work, { recursive: true, force: true });
  }
}

const root = process.argv[2] ? path.resolve(process.argv[2]) : path.resolve(__dirname, '..', '..');
realFiles(root);
plantedFiles();
console.log('');
console.log(`${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
