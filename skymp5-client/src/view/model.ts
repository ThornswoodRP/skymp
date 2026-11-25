import { Movement, Transform } from "../sync/movement";
import { Animation } from "../sync/animation";
import { Appearance } from "../sync/appearance";
import { Equipment } from "../sync/equipment";
import { Inventory } from "../sync/inventory";
import { CreateActorMessageMainProps, CreateActorMessageAdditionalProps } from "src/services/messages/createActorMessage";

// Own properties (not inherited) are being assigned locally
export interface FormModel extends CreateActorMessageAdditionalProps, CreateActorMessageMainProps{
  idx?: number;
  movement?: Movement;
  placedTransform?: Transform;
  numMovementChanges?: number;
  // THORNSWOOD (Thornswood #6247). The highest moveSeq the server numbered a
  // move of this form with that was taken; an older one is dropped.
  lastMoveSeq?: number;
  // THORNSWOOD (Thornswood #6247). One more each time the server says who
  // hosts this form (isHostedByOther), so FormView asks to host it once each
  // time it is freed, however quickly the news came.
  numHostedByOtherChanges?: number;
  numAppearanceChanges?: number;
  isMyClone?: boolean;
}

export interface WorldModel {
  forms: Array<FormModel | undefined>;
  playerCharacterFormIdx: number;
  playerCharacterRefrId: number;
}
