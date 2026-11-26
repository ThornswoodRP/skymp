#pragma once

// Thornswood #6886: writes the person's own character controller state when
// it changes (CharacterStateLog.h). Called on the game's thread each tick.
void LogPlayerMovement();
