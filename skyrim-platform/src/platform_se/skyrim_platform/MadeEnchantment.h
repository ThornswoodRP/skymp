#pragma once

#include "MadePotion.h"

#include <cstddef>

// THORNSWOOD (Thornswood #4513). The game-free half of createMadeEnchantment
// (InventoryApi.cpp): which requests it builds.
//
// An enchantment somebody put on a piece is a form the enchanting game made
// while it ran, so no other session has it. The server keeps it by its
// contents (Inventory::MadeEnchantment in skymp5-server) under an id worked
// out from them, in the same made range as a brewed bottle, and the client
// builds an enchantment with that very id when a piece carrying it arrives.
// A request has the shape of a bottle's (MadePotion::Request) and the same
// rules, with at most two effects: one, or two with Twin Souls, which is all
// the server keeps (CraftService::MadeEnchantment).
namespace MadeEnchantment {

constexpr size_t kMaxEffects = 2;

inline bool IsWellFormed(const MadePotion::Request& r)
{
  return r.effectIds.size() <= kMaxEffects && MadePotion::IsWellFormed(r);
}
}
