import { Game, MagicEffect, printConsole } from 'skyrimPlatform';

/*
  THORNSWOOD PATCH (Thornswood #4579). What disenchanting taught.

  Skyrim keeps a learned enchantment as its magic effects' known flag, in
  the game's own forms, and a session starts from a fresh game, so a
  disenchant was forgotten at every relog. The server now keeps the effects
  on the character (MpChangeForm::learnedEnchantments, CraftService::
  Disenchant) and names them on arrival (props.learnedEnchantments); this
  marks each one known again, so the arcane enchanter offers it. The flag
  belongs to the effect's form, not to the actor, so nothing waits for the
  character to be ready. Answers how many were marked.
*/
export const markEnchantmentsKnown = (effectIds: Array<number>): number => {
  let marked = 0;
  for (const id of effectIds) {
    const effect = MagicEffect.from(Game.getFormEx(id));
    if (!effect) {
      printConsole(`learned enchantment ${id.toString(16)} is no magic effect here`);
      continue;
    }
    effect.setPlayerKnows(true);
    marked++;
  }
  return marked;
};
