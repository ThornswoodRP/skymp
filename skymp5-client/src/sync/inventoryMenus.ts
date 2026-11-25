import { Ui } from "skyrimPlatform";

// Keep the existing public bad-menu policy unchanged. The inventory job also
// waits during lockpicking so an unpaused lockpick cannot admit more units.
export const badInventoryMenus = [
  "InventoryMenu", "FavoritesMenu", "MagicMenu", "ContainerMenu", "Crafting Menu",
] as const;
export const isBadMenuShown = (): boolean => badInventoryMenus.some(name => Ui.isMenuOpen(name));
export const ownerInventoryMenuBlocked = (): boolean => isBadMenuShown() || Ui.isMenuOpen("Lockpicking Menu");
