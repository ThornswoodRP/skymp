#pragma once

#include "NapiHelper.h"

// THORNSWOOD PATCH (Thornswood #4507). KillMoveApi.cpp says what it is.
namespace KillMoveApi {

// getFittingKillMoves(attackerFormId, targetFormId, isPowerAttack): the
// killmoves Skyrim's own condition code judges fit for this attacker on this
// target, in the order the game takes them, with ShouldAttackKill and the
// KillMove global left to the server. An empty array when there are none or
// either actor is not loaded.
Napi::Value GetFittingKillMoves(const Napi::CallbackInfo& info);

void Register(Napi::Env env, Napi::Object& exports);
}
