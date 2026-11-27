import { Actor } from "skyrimPlatform";
import { ApplyDeathStateEvent } from "../events/applyDeathStateEvent";
import { ApplyKillMoveEvent } from "../events/applyKillMoveEvent";
import { ClientListener, CombinedController, Sp } from "./clientListener";
import { RespawnNeededError } from "../../lib/errors";
import { AnimationEventName, StaggerEventName, takeStaggerFromNetwork } from "../../sync/animation";
import { RagdollService } from "./ragdollService";
import { logError, logTrace } from "../../logging";

// Thornswood #4507: GLOB KillMove in Skyrim.esm
const killMoveGlobalId = 0x100f19;
// Thornswood #4507: attackers playing a pair the server sent, whose killmove
// events the hook below lets through
const pairAttackers = new Set<number>();

export class DeathService extends ClientListener {
  constructor(private sp: Sp, private controller: CombinedController) {
    super();
    controller.once("update", () => this.onceUpdate());
    controller.emitter.on("applyDeathStateEvent", (e) => this.onApplyDeathState(e));
    controller.emitter.on("applyKillMoveEvent", (e) => this.onApplyKillMove(e));
    controller.on("update", () => this.onUpdateKillMoves());
    this.hookDisableKillMoves();
    this.hookDisableStagger();
    this.hookDisableBlockedAnims();
  }

  public isBusy() {
    return this.playerDead || this.busyForOtherReasonsCounter > 0;
  }

  private onceUpdate() {
    const player = this.sp.Game.getPlayer();
    player?.startDeferredKill();
    this.switchOffGameKillMoves();
  }

  // THORNSWOOD PATCH (Thornswood #4507). The game's KillMove global gates the
  // four roots its attack killmoves hang under, and nothing else. At 0 no
  // game starts one by itself: the server plays one on every machine near
  // the target when a melee hit's death goes through (KillMoves.cpp in
  // skymp5-server), from the ones the attacker's game judged fit
  // (getFittingKillMoves, HitService).
  private switchOffGameKillMoves() {
    const global = this.sp.GlobalVariable.from(this.sp.Game.getFormEx(killMoveGlobalId));
    if (!global) {
      return logError(this, "switchOffGameKillMoves - no KillMove global", killMoveGlobalId.toString(16));
    }
    global.setValue(0);
  }

  // THORNSWOOD PATCH (Thornswood #4507). The attacker plays the pair on the
  // target. A death that arrives for the target while the pair plays waits
  // for it: the pair is how the target dies on this machine, and killing it
  // first would drop it out of the pair. The pair has ended once the
  // attacker's graph no longer says it is synced (bIsSynced, the variable
  // the killmove roots' own conditions read).
  private onApplyKillMove(e: ApplyKillMoveEvent) {
    const idle = this.sp.Idle.from(this.sp.Game.getFormEx(e.idle));
    if (!idle) {
      return logError(this, "onApplyKillMove - no idle", e.idle.toString(16));
    }
    pairAttackers.add(e.attacker.getFormID());
    const started = e.attacker.playIdleWithTarget(idle, e.target);
    logTrace(this, "onApplyKillMove", e.attacker.getFormID().toString(16), "on",
      e.target.getFormID().toString(16), "idle", e.idle.toString(16), "started", started);
    if (!started) {
      pairAttackers.delete(e.attacker.getFormID());
    } else {
      this.killMoves.set(e.target.getFormID(), {
        attackerId: e.attacker.getFormID(), synced: false, deathWaiting: false
      });
    }
  }

  private onUpdateKillMoves() {
    for (const [targetId, pair] of Array.from(this.killMoves.entries())) {
      const attacker = this.sp.Actor.from(this.sp.Game.getFormEx(pair.attackerId));
      const synced = !!attacker && attacker.getAnimationVariableBool("bIsSynced");
      if (synced) {
        pair.synced = true;
        continue;
      }
      if (!pair.synced) {
        // The pair never showed in the attacker's graph by the update after
        // it was started, so nothing here waits on it: the death goes ahead
        // as it would without one. The log says how often (in-game check
        // for #4507).
        logTrace(this, "onUpdateKillMoves - pair not synced", pair.attackerId.toString(16),
          "on", targetId.toString(16));
      }
      this.killMoves.delete(targetId);
      pairAttackers.delete(pair.attackerId);
      if (pair.deathWaiting) {
        const target = this.sp.Actor.from(this.sp.Game.getFormEx(targetId));
        if (target) {
          this.applyDeathState(target, true);
        }
      }
    }
  }

  private onApplyDeathState(e: ApplyDeathStateEvent) {
    const pair = this.killMoves.get(e.actor.getFormID());
    if (pair && e.isDead) {
      pair.deathWaiting = true;
      return;
    }
    if (pair) {
      this.killMoves.delete(e.actor.getFormID());
      pairAttackers.delete(pair.attackerId);
    }
    this.applyDeathState(e.actor, e.isDead);
  }

  private hookDisableKillMoves() {
    this.sp.hooks.sendAnimationEvent.add(
      {
        enter(ctx) {
          // THORNSWOOD PATCH (Thornswood #4507). A pair the server sent plays;
          // one this game starts by itself on someone else's actor is blanked.
          if (pairAttackers.has(ctx.selfId)) {
            return;
          }
          ctx.animEventName = "";
        },
        leave() { },
      },
      0xff000000,
      0xffffffff,
      "KillMove*"
    );
  }

  private hookDisableStagger() {
    this.sp.hooks.sendAnimationEvent.add(
      {
        enter(ctx) {
          // THORNSWOOD PATCH (Thornswood #489). A stagger the network sent
          // for this actor plays; one this game raised on it is blanked.
          // See staggersFromNetwork in sync/animation.ts.
          if (takeStaggerFromNetwork(ctx.selfId)) {
            return;
          }
          ctx.animEventName = "";
        },
        leave() { },
      },
      0xff000000,
      0xffffffff,
      StaggerEventName
    );
  }

  private hookDisableBlockedAnims() {
    this.sp.hooks.sendAnimationEvent.add(
      {
        enter: (ctx) => {
          if (this.allowedPlayerAnimations === null) {
            return;
          }
          if (!this.allowedPlayerAnimations.includes(ctx.animEventName)) {
            ctx.animEventName = "";
          }
        },
        leave() { },
      },
      this.playerActorId,
      this.playerActorId
    );
  }

  private applyDeathState = (actor: Actor, isDead: boolean) => {
    if (actor.isDead() === isDead && this.isPlayer(actor) === false) {
      return;
    }
    if (isDead === true) {
      this.killActor(actor, null);
    } else {
      this.resurrectActor(actor);
    }
  };

  private killActor = (actor: Actor, killer: Actor | null = null): void => {
    if (this.isPlayer(actor) === true) {
      this.playerDead = true;
      this.busyForOtherReasonsCounter++;
      this.sp.Utility.wait(7.5).then(() => this.busyForOtherReasonsCounter--);
      this.allowedPlayerAnimations = [];
      actor.setDontMove(true);
      this.killWithPush(actor);
    } else {
      actor.endDeferredKill();
      actor.kill(killer);
    }
  };

  private resurrectActor = (actor: Actor): void => {
    if (this.isPlayer(actor) === true) {
      this.playerDead = false;
      this.busyForOtherReasonsCounter++;
      this.sp.Utility.wait(7.5).then(() => this.busyForOtherReasonsCounter--);
      this.allowedPlayerAnimations = null;
      actor.setDontMove(false);
      this.ressurectWithPushKill(actor);
    } else {
      throw new RespawnNeededError("needs to be respawned");
    }
  };

  private killWithPush = (actor: Actor): void => {
    this.allowedPlayerAnimations?.push(AnimationEventName.Ragdoll);
    actor.pushActorAway(actor, 0);
  };

  private ressurectWithPushKill = (act: Actor): void => {
    const formId = act.getFormID();
    const ragdollService = this.controller.lookupListener(RagdollService);
    ragdollService.safeRemoveRagdollFromWorld(act, () => {
      const actor = Actor.from(this.sp.Game.getFormEx(formId));
      if (!actor) {
        return;
      }
      // TODO: should use actor variable instead of getPlayer?
      // TODO: use different iGetUpType if ressurecting under water
      this.sp.Game.getPlayer()!.setAnimationVariableInt("iGetUpType", 1);
      this.sp.Debug.sendAnimationEvent(actor, AnimationEventName.GetUpBegin);
    });
  };

  private isPlayer = (actor: Actor): boolean => {
    return actor.getFormID() === this.playerActorId;
  };

  // Null to allow all animations. Empty array to disallow all
  private allowedPlayerAnimations: string[] | null = null;

  private readonly playerActorId = 0x14;

  private playerDead = false;
  // Thornswood #4507. The pairs playing here, by the target's form id
  private killMoves = new Map<number, { attackerId: number, synced: boolean, deathWaiting: boolean }>();

  private busyForOtherReasonsCounter = 0;
}
