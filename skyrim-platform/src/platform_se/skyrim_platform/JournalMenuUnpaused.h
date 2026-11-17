#pragma once
// Thornswood #6428. Escape must not pause a connected world's game loop.
// Wrap the game's Journal Menu creator before it is opened, so both show
// and hide see kPausesGame removed and the pause count remains balanced.
// All other flags, the menu creator and the game's System page stay intact.
namespace JournalMenuUnpaused {
void Install();
}
