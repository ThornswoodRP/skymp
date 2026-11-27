#pragma once
#include <cstdint>

namespace InventoryApplyMenus {
// Mirrors the client inventoryMenus predicate. A parity regression keeps the
// five existing bad menus and the inventory-only lockpick extension aligned.
template<class IsOpen>
bool Blocks(uint32_t referenceId, IsOpen&& isOpen) {
  if (referenceId != 0x14) return false;
  static constexpr const char* names[] = {
    "InventoryMenu", "FavoritesMenu", "MagicMenu", "ContainerMenu", "Crafting Menu", "Lockpicking Menu"
  };
  for (const auto* name : names) if (isOpen(name)) return true;
  return false;
}
}
