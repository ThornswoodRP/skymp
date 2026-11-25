import { createInventoryApply, cancelInventoryApply, getInventoryQueueFence, on, printConsole } from "skyrimPlatform";

export interface InventoryAddition {
  baseId: number;
  count: number;
  quantityPerUnit: number;
  health: number;
  enchantmentId: number;
  maxCharge: number;
  removeEnchantmentOnUnequip: boolean;
  chargePercent: number;
  name: string;
  soul: number;
  poisonId: number;
  poisonCount: number;
  worn: boolean;
  wornLeft: boolean;
}
export interface InventoryApplyScope {
  isCurrent(): boolean;
  isReady(): boolean;
  state(): "preparing" | "pending" | "drained" | "cancelling" | "cancelled" | "uncertain";
  submit(additions: InventoryAddition[]): void;
  cancel(): void;
}

interface Apply {
  scope: InventoryApplyScope;
  signature: string;
  prepare: () => InventoryAddition[];
  queued: () => boolean;
  prepared: boolean;
  done: boolean;
}
const applications = new Map<number, Apply>();

// Queue position remains a queue-position fence. A full reconciliation must
// ALSO finish its compact native job, whose continuation spans later frames.
const complete = (apply: Apply): boolean => {
  if (apply.done) return true;
  const state = apply.scope.state();
  return state !== "preparing" && state !== "pending" && state !== "cancelling" && apply.scope.isReady() && apply.queued();
};

// Preparation is proportional to inventory ENTRIES, never the units in a
// count. At most one reference is prepared per update; the first reference
// moves to the back so paused/preparing jobs cannot starve later references.
on("update", () => {
  const first = applications.entries().next();
  if (first.done) return;
  const [id, apply] = first.value;
  applications.delete(id);
  if (complete(apply)) { apply.done = true; return; }
  applications.set(id, apply);
  if (!apply.scope.isCurrent()) { apply.scope.cancel(); return; }
  if (apply.prepared || !apply.scope.isReady() || !apply.queued()) return;
  try {
    const additions = apply.prepare();
    if (!apply.scope.isCurrent() || applications.get(id) !== apply) return;
    apply.scope.submit(additions);
    apply.prepared = true;
  } catch (error) {
    // Preparation/native submission can have queued earlier removals. Do not
    // replay a partially applied plan. Its next target is diffed from a fresh
    // actual pack only after both this scope and the queue have settled.
    apply.scope.cancel();
    apply.prepared = true;
    printConsole("Inventory application stopped; actual inventory must be checked before another plan");
  } finally {
    apply.queued = getInventoryQueueFence();
  }
});

export const requestInventoryApply = (id: number, signature: string, prepare: () => InventoryAddition[]): boolean => {
  const previous = applications.get(id);
  if (previous && previous.signature === signature && previous.scope.isCurrent() && !complete(previous)) return false;
  // Native scope creation cancels the previous scope and carries its running
  // unit barrier. It captures the reference handle and load epoch NOW, before
  // waiting/preparation, rather than trusting a later lookup of a reused ID.
  const scope = createInventoryApply(id);
  applications.set(id, { scope, signature, prepare, queued: getInventoryQueueFence(), prepared: false, done: false });
  return false;
};

export const inventoryApplyPending = (id: number): boolean => {
  const apply = applications.get(id);
  return !!apply && !complete(apply);
};

export type InventoryApplyFence = (() => boolean) & { isCurrent(): boolean };
export const inventoryApplyFence = (id: number): InventoryApplyFence => {
  const apply = applications.get(id);
  const queued = getInventoryQueueFence();
  const fence = (() => queued() && (!apply || complete(apply))) as InventoryApplyFence;
  // A caller waiting for this particular target can refuse a cancelled
  // activation without waiting forever for a cleared old-world queue fence.
  fence.isCurrent = () => !apply || apply.scope.isCurrent();
  return fence;
};

export const cancelInventoryApplication = (id: number): void => {
  const apply = applications.get(id);
  if (apply) {
    apply.scope.cancel();
    applications.delete(id);
  }
  if (id) cancelInventoryApply(id);
};

export const cancelAllInventoryApplications = (): void => {
  applications.forEach((_apply, id) => cancelInventoryApplication(id));
};

on("preLoadGame", () => cancelAllInventoryApplications());
on("newGame", () => cancelAllInventoryApplications());
