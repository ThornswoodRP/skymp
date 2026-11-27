import { Actor } from "skyrimPlatform";

// Thornswood #4507. The server picked killmove `idle` for `attacker` on
// `target`; the death that follows comes by the pair.
export interface ApplyKillMoveEvent {
    attacker: Actor;
    target: Actor;
    idle: number;
}
