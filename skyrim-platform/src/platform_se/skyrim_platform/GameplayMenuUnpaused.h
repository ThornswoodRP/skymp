#pragma once

namespace RE {
class ButtonEvent;
}

namespace GameplayMenuUnpaused {
// Called synchronously in SKSEPlugin_Load, before WinMain and UI jobs start.
bool Install();
bool Installed();
// Only requests cross the Node/game-thread boundary. Transport losses do not
// end the world session; an explicit client destruction plus unload does.
void RequestClient(bool requested);
void WorldLoading();
void WorldLoaded(bool success);
bool HasLoadedWorld();
void PostLoad(void* outcome);
void InventoryChanged(bool opened);
void InventoryKey(const RE::ButtonEvent& button);
}
