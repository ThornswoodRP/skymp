import { MsgType } from "../../messages";

export interface Hit {
    aggressor: number;
    isBashAttack: boolean;
    isHitBlocked: boolean;
    isPowerAttack: boolean;
    isSneakAttack: boolean;
    projectile: number;
    source: number;
    target: number;
    // Thornswood #4507: the killmoves the attacker's game judged fit for
    // this swing, in the idle tree's order (HitMessage.h)
    killMoves?: number[];
}

export interface HitMessage {
    t: MsgType.OnHit,
    data: Hit
}
