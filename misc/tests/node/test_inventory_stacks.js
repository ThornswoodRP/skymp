'use strict';
// Thornswood #7175. The same item stood in the pack as several rows,
// "Firewood (5)" and "Firewood (3)", instead of one row of 8.
//
// applyInventory gave addItemEx the form's own name for an item the server
// sends with no name, and addItemEx makes an extra list for every add that
// carries a name, so each add of the same item came with a list of its own,
// and the game shows each list as a row. This loads the real inventory.ts
// over a stand-in game whose addItemEx does what the native one does (a
// list when the add carries a name, a health over 1, an enchantment, a
// charge, a soul or a poison; plain units otherwise) and checks:
//   - Firewood added five at one time and three at another is one row of 8
//     with no list, and the next pass finds nothing to do;
//   - a book, a key and a potion with no name join their plain units too;
//   - an item the server names keeps its name and its list;
//   - a sword and boots are still added under the form's name, as before
//     (their condition goes on the name, #1651), and a worn piece too;
//   - the plain units are taken away as plain units.
// INVENTORY=<file> runs it against another inventory.ts (the one before the
// change fails the stacking cases).
const fs = require('node:fs'), path = require('node:path'), vm = require('node:vm');
const ts = require('../../../skymp5-client/node_modules/typescript');
const file = process.env.INVENTORY ||
  path.join(__dirname, '../../../skymp5-client/src/sync/inventory.ts');
const js = ts.transpileModule(fs.readFileSync(file, 'utf8'), {
  compilerOptions: { module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2018 },
}).outputText;

let pass = 0, fail = 0;
function yes(what, cond) {
  if (cond) { pass++; console.log('PASS  ' + what); } else { fail++; console.log('FAIL  ' + what); }
}

const SWORD = 0x12eb7, BOOTS = 0x12e4b, GOLD = 0xf, FIREWOOD = 0x6f993, BOOK = 0x1afd9, KEY = 0x2f2bd, STEW = 0xf4314;
const ENCHANTMENT = 0x1234, POISON = 0x2345;
const NAMES = { [SWORD]: 'Iron Sword', [BOOTS]: 'Boots', [GOLD]: 'Gold', [FIREWOOD]: 'Firewood',
  [BOOK]: 'The Lusty Argonian Maid', [KEY]: 'Riverwood Trader Key', [STEW]: 'Vegetable Soup' };
const FormType = { Misc: 32, Potion: 46, Ingredient: 30, Weapon: 41, Armor: 26, Book: 27, SoulGem: 52, Key: 45 };
const TYPES = { [GOLD]: FormType.Misc, [FIREWOOD]: FormType.Misc, [BOOTS]: FormType.Armor, [BOOK]: FormType.Book,
  [KEY]: FormType.Key, [STEW]: FormType.Potion };
const BASE = 7;

// The game: the character's extra lists (0x14), each { baseId, count, name,
// health, worn }, and units with no list of their own in the base container
// (base) and in the changes (loose). A removal by a list the game does not
// hold (addItemEx with a negative count) takes plain units first and never a
// worn list: one choice the game can make, and the one that kept the boots.
function makeGame() {
  const game = { lists: [], base: {}, loose: {}, calls: [], wornState: null, console: [], nodeUpdates: 0 };
  class Form {
    constructor(id) { this.id = id; }
    getFormID() { return this.id; }
    getName() { return NAMES[this.id] || ''; }
    getType() { return TYPES[this.id] || FormType.Weapon; }
  }
  class ObjectReference extends Form {
    getBaseObject() { return new Form(BASE); }
    removeAllItems() { game.calls.push(['removeAllItems']); }
  }
  const pc = new ObjectReference(0x14);
  const takePlain = (baseId, n) => {
    for (const where of [game.loose, game.base]) {
      const k = Math.min(n, where[baseId] || 0);
      where[baseId] = (where[baseId] || 0) - k;
      n -= k;
    }
    return n;
  };
  const sp = {
    Form, ObjectReference, FormType,
    Game: { getFormEx: (id) => (NAMES[id] || id === ENCHANTMENT || id === POISON ? new Form(id) : null) },
    Ammo: { from: () => null }, Actor: { from: (r) => r && Object.assign(r, { queueNiNodeUpdate() { game.nodeUpdates++; } }) },
    Enchantment: { from: f => f }, Potion: { from: f => f },
    printConsole: (...a) => { game.console.push(a.join(' ')); },
    storage: { basesResetExists: true, basesReset: new Set([BASE]) },
    getContainer: (id) => (id === BASE
      ? Object.keys(game.base).filter((k) => game.base[k] > 0).map((k) => ({ baseId: Number(k), count: game.base[k] }))
      : []),
    getExtraContainerChanges: () => {
      const byBase = new Map();
      const entry = (baseId) => {
        if (!byBase.has(baseId)) byBase.set(baseId, { baseId, countDelta: 0, extendDataList: [] });
        return byBase.get(baseId);
      };
      for (const l of game.lists) {
        const e = entry(l.baseId);
        e.countDelta += l.count;
        const extras = [];
        if (l.count !== 1) extras.push({ type: 'Count', count: l.count });
        if (l.name) extras.push({ type: 'TextDisplayData', name: l.name });
        if (l.health) extras.push({ type: 'Health', health: l.health });
        if (l.worn) extras.push({ type: 'Worn' });
        if (l.wornLeft) extras.push({ type: 'WornLeft' });
        if (l.enchantmentId) extras.push({ type: 'Enchantment', enchantmentId: l.enchantmentId,
          maxCharge: l.maxCharge, removeOnUnequip: l.removeEnchantmentOnUnequip });
        if (l.chargePercent) extras.push({ type: 'Charge', charge: l.chargePercent });
        if (l.soul) extras.push({ type: 'Soul', soul: l.soul });
        if (l.poisonId) extras.push({ type: 'Poison', poisonId: l.poisonId, count: l.poisonCount });
        e.extendDataList.push(extras);
      }
      for (const k of Object.keys(game.loose)) entry(Number(k)).countDelta += game.loose[k];
      return [...byBase.values()];
    },
    renameInventoryItems: () => false,
    removeInventoryItems: (refrId, baseId, listCount, indexes, counts, plain) => {
      game.calls.push(['remove', baseId, indexes.slice(), counts.slice(), plain]);
      if (game.arrives) { game.lists.push(game.arrives); game.arrives = null; }
      const lists = game.lists.filter((l) => l.baseId === baseId);
      if (lists.length !== listCount) return false;
      indexes.forEach((i, k) => { lists[i].count -= counts[k]; });
      game.lists = game.lists.filter((l) => l.count > 0);
      // what the base container held and is gone is a negative delta
      game.loose[baseId] = (game.loose[baseId] || 0) - plain;
      return true;
    },
    TESModPlatform: {
      resetContainer() {},
      pushWornState(worn, wornLeft) { game.wornState = { worn, wornLeft }; },
      addItemEx(refr, f, count, health, ench, maxCharge, rm, charge, name, soul, poison, poisonCount) {
        const worn = !!(game.wornState && game.wornState.worn);
        const wornLeft = !!(game.wornState && game.wornState.wornLeft);
        game.wornState = null;
        game.calls.push(['addItemEx', f.getFormID(), count, name, health, worn]);
        if (count > 0) {
          // The native makes a list only for an add that carries something
          // (PapyrusTESModPlatform.cpp, AddItemEx); a bare add is plain units.
          if (!name && !(health > 1) && !ench && !(charge > 0) && !(soul > 0 && soul <= 5) && !poison) {
            game.loose[f.getFormID()] = (game.loose[f.getFormID()] || 0) + count;
            return;
          }
          // the game keeps a float: 1.2000000476837158 is 1.2 to it
          const l = { baseId: f.getFormID(), count, name };
          if (health > 1) l.health = Math.fround(health);
          if (worn) l.worn = true;
          if (wornLeft) l.wornLeft = true;
          if (ench) Object.assign(l, { enchantmentId: ench.getFormID(), maxCharge, removeEnchantmentOnUnequip: rm });
          if (charge) l.chargePercent = charge;
          if (soul) l.soul = soul;
          if (poison) Object.assign(l, { poisonId: poison.getFormID(), poisonCount });
          game.lists.push(l);
          return;
        }
        let left = takePlain(f.getFormID(), -count);
        for (const l of game.lists) {
          if (left && !l.worn && l.baseId === f.getFormID() && (l.name || '') === name &&
              Math.fround(l.health || 1) === Math.fround(health || 1)) {
            const n = Math.min(left, l.count); l.count -= n; left -= n;
          }
        }
        game.lists = game.lists.filter((l) => l.count > 0);
      },
    },
  };
  const sandbox = { exports: {}, require: (m) => (m === 'skyrimPlatform' ? sp : {}) };
  sandbox.module = { exports: sandbox.exports };
  vm.runInNewContext(js, sandbox);
  game.inv = sandbox.exports;
  game.pc = pc;
  game.apply = (entries) => game.inv.applyInventory(pc, { entries }, false, true);
  game.diff = (entries) => game.inv.getDiff({ entries }, game.inv.getInventory(pc), true).entries;
  return game;
}


const rows = (g, baseId) => g.lists.filter((l) => l.baseId === baseId).length + ((g.loose[baseId] || 0) > 0 ? 1 : 0);
const held = (g, baseId) => g.lists.filter((l) => l.baseId === baseId).reduce((n, l) => n + l.count, 0) + (g.loose[baseId] || 0);

// Firewood: five from one place, then three from another.
{
  const g = makeGame();
  g.apply([{ baseId: FIREWOOD, count: 5 }]);
  g.apply([{ baseId: FIREWOOD, count: 8 }]);
  yes('Firewood added five at one time and three at another is one row', rows(g, FIREWOOD) === 1);
  yes('  of 8', held(g, FIREWOOD) === 8);
  yes('  with no list of its own', g.lists.every((l) => l.baseId !== FIREWOOD));
  yes('  and each add carried no name', g.calls.filter((c) => c[0] === 'addItemEx').every((c) => c[3] === ''));
  g.calls.length = 0;
  g.apply([{ baseId: FIREWOOD, count: 8 }]);
  yes('  and the next pass finds nothing to do', g.calls.length === 0);
}

// Taking some away takes plain units.
{
  const g = makeGame();
  g.apply([{ baseId: FIREWOOD, count: 8 }]);
  g.calls.length = 0;
  g.apply([{ baseId: FIREWOOD, count: 2 }]);
  const removes = g.calls.filter((c) => c[0] === 'remove');
  yes('six taken away from the row of 8 are taken as plain units',
    removes.length === 1 && removes[0][1] === FIREWOOD && removes[0][2].length === 0 && removes[0][4] === 6);
  yes('  and 2 are left in one row', held(g, FIREWOOD) === 2 && rows(g, FIREWOOD) === 1);
}

// A book, a key and a potion with no name stack the same way.
for (const [id, what] of [[BOOK, 'a book'], [KEY, 'a key'], [STEW, 'a potion (soup)']]) {
  const g = makeGame();
  g.apply([{ baseId: id, count: 1 }]);
  g.apply([{ baseId: id, count: 3 }]);
  yes(what + ' added one at a time is one row of 3', rows(g, id) === 1 && held(g, id) === 3);
}

// A name the server gives stays on the item, in its list.
{
  const g = makeGame();
  g.apply([{ baseId: KEY, count: 1, name: 'Key to Breezehome' }]);
  yes('a key the server names keeps the name in a list of its own',
    g.lists.length === 1 && g.lists[0].name === 'Key to Breezehome' && !(g.loose[KEY] > 0));
  g.apply([{ baseId: KEY, count: 1, name: 'Key to Breezehome' }, { baseId: KEY, count: 2 }]);
  yes('  and plain keys beside it are a row of their own', rows(g, KEY) === 2 && held(g, KEY) === 3);
}

// Gear keeps the form's name, as before: its condition goes on the name.
{
  const g = makeGame();
  g.apply([{ baseId: SWORD, count: 1 }]);
  const add = g.calls.find((c) => c[0] === 'addItemEx');
  yes('a sword with no name from the server is still added under the form\'s name',
    !!add && add[3] === 'Iron Sword' && g.lists.length === 1);
}
{
  const g = makeGame();
  g.apply([{ baseId: BOOTS, count: 1, worn: true }]);
  const add = g.calls.find((c) => c[0] === 'addItemEx');
  yes('worn boots are still added under the form\'s name, worn', !!add && add[3] === 'Boots' && add[5] === true);
}

console.log(pass + ' passed, ' + fail + ' failed');
process.exitCode = fail ? 1 : 0;
