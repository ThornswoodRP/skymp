#pragma once
#include <cstdint>
#include <optional>
#include <vector>

namespace espm {
class CombineBrowser;
class CompressedFieldsCache;
}

// Thornswood #4507. Killmoves play on every machine near the kill.
//
// The killmoves the server plays are those under the four roots the game
// switches with its KillMove global (GetGlobalValue(KillMove) == 1 is on
// these four and nowhere else): KillMoveFrontSideRoot00 and
// KillMoveBackSideRoot00 under AttackRightRoot > NonMountedCombatRight for a
// normal attack, and KillMoveFrontSideRoot and KillMoveBackSideRoot under
// PowerAttackRoot > NonMountedCombatRightPower for a power attack. Every
// machine sets KillMove to 0, so no game starts one of these by itself.
//
// Which of them fit a swing is the attacker's game's to judge, by Skyrim's
// own condition code: an attacker's equipment, its animation graph, whether
// it is staggered, the target's furniture pose and whether the target is the
// last enemy are all known exactly there. The server judges the two
// conditions that are its own: ShouldAttackKill (the hit's death really goes
// through, after mp.onDying) and the KillMove global (killmoves are on).
namespace KillMoves {

// True when idleId is an IDLE record that names an animation event and
// whose chain up the idle tree runs through one of the killmove roots of
// this kind of attack.
bool FitsAttack(const espm::CombineBrowser& browser,
                espm::CompressedFieldsCache& cache, uint32_t idleId,
                bool isPowerAttack);

// The first of the attacker's candidates, in the order its game sent them
// (the idle tree's order, the order the game itself takes them in), that
// FitsAttack. Nothing when none does.
std::optional<uint32_t> Pick(const espm::CombineBrowser& browser,
                             espm::CompressedFieldsCache& cache,
                             const std::vector<uint32_t>& candidates,
                             bool isPowerAttack);

}
