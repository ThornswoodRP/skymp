import { Inventory } from "skyrimPlatform";
import { MsgType } from "../../messages";
import { Made, MadeEnchantment } from "../../sync/inventory";

export interface CraftItemMessage {
    t: MsgType.CraftItem,
    data: CraftItemMessageData
}

interface CraftItemMessageData {
    workbench: number;
    craftInputObjects: Inventory;
    resultObjectId: number;
    // Thornswood #1649: what is in a bottle the game brewed this session.
    resultPotion?: Made;
    // Thornswood #4513: the enchantment the game put on a piece this session,
    // and the charge the piece holds at most (a weapon's).
    resultEnchantment?: MadeEnchantment;
    resultMaxCharge?: number;
}
