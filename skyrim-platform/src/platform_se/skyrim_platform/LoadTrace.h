#pragma once

/*
  THORNSWOOD PATCH (#6200): where the game's start goes, measured, from the
  plugin's load to the game's data load. On 26 Nov 2026 the page drew at
  16:13:34.9 and the data load came at 16:13:43.96, and no cursor can come up
  before it, so the first screen sat nine seconds with no cursor. A second
  thread looks at the game's main thread every 10 ms: which module it is in,
  which plugin file the game is reading, and the time, processor and disk
  each step between SKSE messages took. At the data load it writes the
  totals to skyrim-platform.log as "[load]" lines and stops. It changes
  nothing in the game; it is taken out once its cause is fixed.
*/
namespace LoadTrace {
// On the game's main thread, in SKSEPlugin_Load.
void Start();

// Each SKSE message, as the game's main thread passes it to plugins. The
// data load's message ends the trace.
void Mark(uint32_t skseMessageType);
}
