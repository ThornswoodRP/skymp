import { MsgType } from "../../messages";
import { Movement } from "../../sync/movement";

export interface UpdateMovementMessage {
    t: MsgType.UpdateMovement;
    idx: number;
    // THORNSWOOD. teleportSeq: own character only, see
    // RemoteServer.getCarriedOutTeleportSeq.
    // THORNSWOOD (Thornswood #6247). moveSeq and settled: moves are sent on
    // change, see SendInputsService.sendMovement.
    data: Movement & { teleportSeq?: number; moveSeq?: number; settled?: boolean };
}
