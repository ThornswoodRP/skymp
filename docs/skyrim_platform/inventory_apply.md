# Complete inventory application across frames

The Thornswood client uses an inventory-specific coordinator for additions.
A large count stays one compact descriptor. Weapons and armour still receive
one native extra list per piece, including the existing name, temper,
enchantment, charge, poison, soul and worn rules. The legacy AddItemEx API
retains its deferred behavior.

The shared native pump permits at most 16 mutations per update and checks a
two millisecond steady-clock window between indivisible operations. An add
with a possible equip reserves two mutations before starting. The final
appearance refresh also consumes a mutation. An individual native operation
can overrun the cooperative time window; it cannot be interrupted safely.
References rotate after each unit. Paused jobs yield rather than spinning.

JavaScript prepares at most one reference per update, rotating references
that are waiting. Preparation and logs depend on inventory entries, not on
the number of pieces in a count. Miscellaneous items, potions, ingredients
and ammunition retain bulk additions; counts are split only at the native
signed int32 argument limit. Gear remains unit-sized.

## Native scope API

`createInventoryApply(referenceId)` must run in a native update context with
a loaded world. It returns a scope with these methods:

- `isCurrent()` checks the captured native reference handle, pointer, existing
  inventory load epoch, current life state and loaded-world state.
- `isReady()` means an earlier running unit for this reference has exited.
  It does not mean the target inventory has arrived.
- `submit(additions)` accepts a compact array once, after validating the whole
  array. The client contract is `InventoryAddition` in
  `skymp5-client/src/sync/inventoryApplyCoordinator.ts`.
- `state()` returns preparing, pending, drained, cancelling, cancelled or
  uncertain. Cancelling remains visible until an active unit returns.
- `cancel()` prevents remaining work. `cancelInventoryApply(referenceId)`
  cancels the current scope for that reference.

A replacement scope obtains its barrier from the canonical scheduler even
if an intermediate JavaScript handle has been collected. Native pre-load,
new-game, death, plugin reload and environment cleanup cancel old work.
Pre-load closes the existing atomic world-admission gate before advancing
the existing inventory load epoch, before any deferred JavaScript event.

An operation that throws may already have changed the pack. Its job becomes
uncertain and never replays that unit. Any later plan must first read the
actual pack and calculate a new difference. An unavailable observation does
not justify another add. There is no fallback to an unbounded unit loop.

## Completion and lifecycle callers

`getInventoryQueueFence()` retains its original queue-position meaning. The
first inventory chunk can precede that fence while later chunks remain.
Use the client coordinator's `inventoryApplyFence(referenceId)` to wait for
both the queued work and the complete captured job. Terminal cancellation
or uncertainty still requires actual inventory comparison; it is not proof
of a successful reconciliation.

The owner spawn path waits before reading another difference or declaring
the outfit ready. Crafting activation waits for the complete job and then
checks the latest server target against a newly acquired player wrapper.
It refuses a stale or incomplete activation. New targets, destroyed views,
death messages, connection changes and explicit loads cancel prior work.

## Remaining runtime acceptance

The TypeScript fixtures count native boundary calls, not Skyrim frame cost.
Hosted tests compile the actual portable scheduler, pump and scope source.
Controlled Dev acceptance must still exercise 1,001, 2,048 and the existing
100,000-piece workload, responsiveness of unrelated actions, metadata and
worn identity, replacement targets, and reconnect/character changes.
Accepting a uint32 count does not establish that billions of pieces are
practical in the native game.
