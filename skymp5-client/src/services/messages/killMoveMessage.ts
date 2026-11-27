import { MsgType } from "../../messages";

// Thornswood #4507. A melee hit killed and the server picked a killmove for
// it: the actor at idx plays the pair on the one at targetIdx. It arrives
// before the death. See KillMoveMessage.h in skymp5-server.
export interface KillMoveMessage {
    t: MsgType.KillMove;
    idx: number;
    targetIdx: number;
    idle: number;
}
