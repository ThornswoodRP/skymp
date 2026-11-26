#pragma once

namespace InteractionLoadCancel {
// Called synchronously in SKSEPlugin_Load, before the game runs any AI.
// Thornswood #6884: see platform_lib/InteractionLoadPatch.h.
bool Install();
}
