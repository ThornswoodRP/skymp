import { FormType } from "skyrimPlatform";

export class FormTypeEx {
  static isItem(type: FormType) {
    return type === FormType.Ammo ||
      type === FormType.Armor ||
      type === FormType.Book ||
      type === FormType.Ingredient ||
      type === FormType.Light ||
      type === FormType.Potion ||
      type === FormType.ScrollItem ||
      type === FormType.SoulGem ||
      type === FormType.Weapon ||
      type === FormType.Misc ||
      // Thornswood #7171: a key lying in the world goes through the server,
      // as the server now counts a key as an item.
      type === FormType.Key;
  }
}
