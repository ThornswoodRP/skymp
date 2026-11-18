# Gameplay menus and the live world

Thornswood #6497 reports that Tab, Inventory and Magic pause the local game.
The older Favorites and Journal creator wrappers cover only those two menus.
The new policy runs after each native Show callback, before the native pause
counter changes. It removes only `kPausesGame` (bit 0) and frozen background
(bit 5) in an entered multiplayer world. Cursor, modal, input, saving and
freeze-frame screenshot flags, including bit 24, retain their original values.
Bit 24 is not the bit used by the UI's pause counter; it is not cleared on the
strength of its name.

## Native evidence and scope

The supported executable is Skyrim SE 1.6.1170.0. Address Library identifiers
come from the pinned CommonLibSSE headers at
`b93280e832f263dbef44e44cbe2936622a02f91a`.
Read-only examination of the running executable confirms:

- UI messages, ID 82082: creator call at `+638`, PostCreate at `+657`,
  ProcessMessage(Show) at `+6BA`, pause-bit test at `+835`, increment at `+839`.
  The hide path decrements the same UI counter at `+3B1`.
- The Show call and comparison are `FF 50 20 83 F8 01`. The trampoline calls
  the original virtual method once through the C++ helper, preserves its EAX
  result, replays `CMP EAX,1` and resumes at `+6C0`. It uses the native call
  site's aligned stack and shadow space without an extra synthetic return.
- Stats ProcessMessage, ID 52510, writes background bit 5 at `+A10`.
  Its hook preserves RSI/R11 and replays the displaced comparison.
- Map local update, ID 53112, adds background bit 5 at `+95`. Its hook retains
  the native store at `+9D`. The cursor-bit write at `+53` is untouched.
- Stats input, ID 52518 `+46`, normally rejects worker-thread UI mode. The
  policy bypasses that one gate only in a live world whose UI is now owned by
  the main thread; offline it replays the original AL test and branch.

These are exact runtime guards, not offsets presumed compatible with another
executable. Every displaced instruction is checked before any is changed.
Unsupported or conflicting bytes leave the whole hook set uninstalled, and
DataLoaded retains the old Favorites/Journal wrappers. No pause-counter reset
or repeated repair of an already-open menu is used.

Sleeping/waiting fast-forward independently requests a pause after Show. The
native Accept function (ID 52487) registers OK at ID 52488 and Cancel at ID
52489. Read-only native tracing confirms OK starts sleep/wait, writes requested
hours into the player and marks the menu active. Cancel only queues Hide.
After original Show initializes the delegate, the candidate verifies the OK
entry/prologue, the complete Cancel body and the callback handler ownership,
then wraps OK. Live confirmations call verified Cancel before explaining that
time cannot be skipped, without calling native OK or touching rest/needs rates.
Offline confirmations call the exact retained original with unchanged arguments.
Repeated Show never captures the wrapper as its own original.
Supported offline Shows also install the wrapper, retaining that exact offline
dispatch if a picker outlives its original session. On live activation an
already-open picker is queued for ordinary Hide before the sole messages pass.
Native AddMessage (13631) pushes onto the same queue that ProcessMessages
(82082) drains before movies, so the old pause is decremented by native Hide.
There is no post-open flag change or counter repair. If the picker refuses to
close, movie/input advancement is blocked and separately reported as a failed
handoff with refused passes and counter mismatches; this is not successful
all-menu acceptance. No repeated hide request is issued.

An unsupported live callback/owner/body is left untouched and Show returns
native Ignore before stack insertion/counting, with a queued Hide and message.
It cannot receive normal menu input. If Cancel changes after a supported Show,
live dispatch uses the queue-hide API instead; it never falls through to OK.
An offline unsupported callback remains untouched. Physical bed occupancy and
real server rest behavior still require native acceptance; the callback fixture
does not prove bed recovery or claim that cancelling preserves bed occupancy.

## UI scheduling

[Skyrim Souls RE's 1.6.1170 implementation](https://github.com/Vermunds/SkyrimSoulsRE/blob/714c44301d9e278b3d0a609fb291de50cd17cf45/src/MenuProcessing.cpp)
documents why unpaused menu processing must run on the main thread after
rendering, when worker jobs should no longer be running. Its code is GPL-3.0-or-later with a modding
exception, compatible with Skyrim Platform's GPL distribution.

The candidate uses that after-render phase, Main Update ID 36564 at `+ADF`, calling
the displaced original function first and exactly once. That function increments
ScrapHeap.keepPagesRequest; the increment itself is not a job-wait barrier.
The owner then runs one messages,
movies and console pass per frame. This deliberately moves paused/title
messages after rendering too. The UI job at ID 39042 `+B`, Main's earlier
message call at `+A8A`, and its movies/console calls at `+AF0..B02` are disabled
as one checked set. Other rendering and Main logic remain intact.

The three native function entries have passive Gum observers. The bounded
`menu UI owner` summary records owner frames, actual function invocations,
per-frame count mismatches and calls from outside the owner. Native acceptance
must demonstrate one pass per owner frame and zero mismatches/outside calls
through title, loading, creation and gameplay menus. The portable test alone
cannot prove engine scheduling.

## Installation and session lifetime

Installation runs synchronously in SKSEPlugin_Load_Impl, beside the existing
native hooks, **before WinMain**. This is a quiescent startup installation,
not an assertion that nine separate instruction writes are atomic. Official
[SKSE 2.2.6 source](https://github.com/ianpatt/skse64/blob/9398d04592a7eb9d754f2997701116df1022f1b4/skse64/skse64.cpp#L49)
calls Initialize from the CRT command-line hook before returning WinMain's
argument. Initialize calls PluginManager.Init, whose InstallPlugins invokes
SKSEPlugin_Load synchronously. Skyrim Platform's entry DLL forwards that call
to Impl immediately. Main Update and the game's UI jobs have not begun.
Only addresses, bytes, private trampoline memory and Gum are accessed during
installation; UI/player objects are first accessed from the later main loop.
All code pages are prepared before any instruction is written, with the
original protections restored on failure.

CreateClient publishes an atomic request. Network disconnections do not
withdraw it. DestroyClient withdraws the request, but a live world retains
the session until unload; this covers a destroy/recreate reconnect without
reintroducing pause. NewGame or successful PostLoad marks a world entered;
PreLoad and failed PostLoad clear it. Main Menu Show explicitly ends the old
lifetime even if stale player/3D pointers remain. Initial activation also requires
loaded player 3D and absence of the title menu. Once active, a temporary 3D rebuild
cannot end the session or reintroduce pause without an explicit unload. Loading a replacement world
does not discard a requested client session. Other modal menus have no
exemption. Only atomic requests/lifecycle bits cross threads; held-session
state and native menu flags belong to the main UI owner. A monotonic unload
epoch prevents PreLoad followed by PostLoad between two owner passes from
hiding the unload. The title never overwrites a CreateClient request that
could precede its initial Show. SKSE 2.2.6 PostLoad encodes success directly
as pointer value 1 (Hooks_SaveLoad.cpp:51); it is compared with null, never
dereferenced.

## Checks

The compiled regression first failed 16 flag/count assertions at fork PR170
head `103bf9ce`. The green fixture exercises the actual policy and executes
the actual emitted x64 Show, Map, Stats and Stats-input stubs in private fixture pages on
Windows and Linux. It checks original-call count, Windows x64 arguments,
EAX/comparison preservation, R11/RSI preservation, unrelated flags,
offline behavior and reconnect/unload transitions.
The actual sleep/wait dispatch is exercised with native-OK pause/time-jump
side effects, verified Cancel, changed Cancel fallback, original offline calls,
unsupported/missing callback and owner, repeated binding and unbound dispatch.
The actual lifecycle boundary also covers initial-title ordering, a complete
load replacement between owner passes, and SKSE's null/value-1 payload.

Full Windows engine compilation and Linux checks are required. A normal Dev
run must additionally verify initial title, load, fresh creation containment,
Tab navigation/cursor/close, Inventory, Magic, world/local Map, Skills, nested
menus, sleeping/waiting, physical bed/server rest, and native UI-pass counters. Main remains held pending that evidence
and the other open release conditions.
