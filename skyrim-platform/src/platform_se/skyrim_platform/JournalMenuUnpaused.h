#pragma once
// Thornswood #6428. Escape must not pause a connected world's game loop.
// Wrap the game's Journal Menu creator before it is opened, so both show
// and hide see kPausesGame removed and the pause count remains balanced.
// Its frozen background is removed too, so the world remains visible as it
// moves. All other flags, the creator and the System page stay intact.
namespace JournalMenuUnpaused {
void Install();
}
