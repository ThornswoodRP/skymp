import { ClientListener, CombinedController, Sp } from "./clientListener";
import { SinglePlayerService } from "./singlePlayerService";
import { FormModel, WorldModel } from "../../view/model";
import { MsgType } from "../../messages";
import { getMovement } from "../../sync/movementGet";

// TODO: refactor this out
import * as worldViewMisc from "../../view/worldViewMisc";

import { Animation, AnimationSource } from "../../sync/animation";
import { Actor, EquipEvent, FormType } from "skyrimPlatform";
import { getAppearance } from "../../sync/appearance";
import { ActorValues, getActorValues } from "../../sync/actorvalues";
import { getEquipment, wornList } from "../../sync/equipment";
import { nextHostAttempt } from "../../view/hostAttempts";
import { traceHostedSend } from "../../view/spawnTrace";
import { SkympClient } from "./skympClient";
import { MessageWithRefrId } from "../events/sendMessageWithRefrIdEvent";
import { UpdateMovementMessage } from "../messages/updateMovementMessage";
import { ChangeValuesMessage } from "../messages/changeValuesMessage";
import { UpdateAnimationMessage } from "../messages/updateAnimationMessage";
import { UpdateEquipmentMessage } from "../messages/updateEquipmentMessage";
import { UpdateAppearanceMessage } from "../messages/updateAppearanceMessage";
import { RemoteServer } from "./remoteServer";
import { DeathService } from "./deathService";
import { logTrace } from "../../logging";

const playerFormId = 0x14;

// TODO: split this service into EquipmentService, MovementService, AnimationService, ActorValueService, HostAttemptsService
export class SendInputsService extends ClientListener {
    constructor(private sp: Sp, private controller: CombinedController) {
        super();
        this.controller.on("update", () => this.onUpdate());
        this.controller.on("equip", (e) => this.onEquip(e));
        this.controller.on("unequip", (e) => this.onUnequip(e));
        this.controller.on("loadGame", () => this.onLoadGame());
        // THORNSWOOD (Thornswood #6247). A new connection is sent every
        // actor's place once, changed or not.
        this.controller.emitter.on("connectionAccepted", () => this.lastMovementSent.clear());
    }

    private onUpdate() {
        if (!this.singlePlayerService.isSinglePlayer) {
            this.sendInputs();

            const player = this.sp.Game.getPlayer()!;
            const isPlayerCasting = player.getAnimationVariableBool("IsCastingRight")
                || player.getAnimationVariableBool("IsCastingLeft")
                || player.getAnimationVariableBool("IsCastingDual");

            if (isPlayerCasting) {
                this.prevCastingDetectedTime = Date.now();
            }
        }
    }

    private onEquip(event: EquipEvent) {
        if (!event.actor || !event.baseObj) {
            return;
        }

        if (event.actor.getFormID() !== playerFormId) {
            return;
        }

        const type = event.baseObj.getType();
        if (type !== FormType.Book && type !== FormType.Potion && type !== FormType.Ingredient) {
            // Trigger UpdateEquipment only for equips that are not spell tomes, potions, ingredients
            this.equipmentChanged = true;
        }

        // Send OnEquip for any equips including spell tomes, potions, ingredients
        // Otherwise, the server won't trigger spell learn, potion drink, ingredient eat and Papyrus scripts
        this.controller.emitter.emit("sendMessage", {
            message: { t: MsgType.OnEquip, baseId: event.baseObj.getFormID() },
            reliability: "unreliable"
        });
    }

    private onUnequip(event: EquipEvent) {
        if (!event.actor || !event.baseObj) {
            return;
        }

        if (event.actor.getFormID() === playerFormId) {
            this.equipmentChanged = true;
        }
    }

    private onLoadGame() {
        // Currently only armor is equipped after relogging (see remoteServer.ts)
        // This hack forces sending /equipment without weapons/ back to the server
        this.sp.Utility.wait(3).then(() => (this.equipmentChanged = true));
    }

    private sendInputs() {
        const hosted =
            typeof this.sp.storage['hosted'] === typeof [] ? this.sp.storage['hosted'] : [];
        const targets = [undefined].concat(hosted as any);

        const modelSource = this.controller.lookupListener(RemoteServer);

        const world = modelSource.getWorldModel();

        // A target this game stopped hosting starts afresh if it hosts it
        // again: its first move then always goes (sendMovement).
        const current = new Set(targets.map((target) => `${target}`));
        for (const k of Array.from(this.lastMovementSent.keys())) {
            if (!current.has(k)) {
                this.lastMovementSent.delete(k);
            }
        }

        targets.forEach((target) => {
            const targetFormModel = target ? this.getForm(target, world) : this.getForm(undefined, world);
            this.sendMovement(target, targetFormModel);
            this.sendAnimation(target);
            this.sendAppearance(target);
            this.sendEquipment(target);
            this.sendActorValuePercentage(target, targetFormModel);
        });
        this.sendHostAttempts();
    }

    /*
      THORNSWOOD (Thornswood #6247). A move is sent when it changes, not every
      130 ms.

      This looked at the actor every 130 ms and sent what it saw, changed or
      not, for this character and for every NPC this game hosts, and the
      server passed each one on to everybody near. In the load test with 100
      people at one spot that was 99% of everything the server sent. Still
      people and still NPCs now send nothing.

      It still looks every 130 ms, the rate a moving actor is sent at. A look
      that differs from the last move sent goes unreliably, as before: the
      next look replaces it. The first look that finds nothing changed sends
      that move once more as settled, reliably, because nothing replaces it;
      then nothing is sent until something changes. Every move carries
      moveSeq, so the server drops one overtaken on the way.

      What counts as a change is held to what anybody could see: a whole unit
      of place (about 1.4 cm), a whole degree of facing, a whole percent of
      health, so the physics engine's settling noise is not a move.
    */
    private sendMovement(_refrId?: number, form?: FormModel) {
        const owner = this.getInputOwner(_refrId);
        if (!owner) {
          return;
        }

        const refrIdStr = `${_refrId}`;
        const sendMovementRateMs = 130;
        const now = Date.now();
        const last = this.lastSendMovementMoment.get(refrIdStr);
        if (last && now - last <= sendMovementRateMs) {
            return;
        }
        this.lastSendMovementMoment.set(refrIdStr, now);

        const data: UpdateMovementMessage["data"] = getMovement(owner, form);
        if (_refrId === undefined) {
            // THORNSWOOD. Tells the server which of its teleports this
            // packet was sent after (ActionListener::OnUpdateMovement).
            data.teleportSeq = this.controller.lookupListener(RemoteServer).getCarriedOutTeleportSeq();
        }

        const key = movementKey(data, form?.idx);
        const sent = this.lastMovementSent.get(refrIdStr);
        let settled = false;
        if (sent && sent.key === key) {
            if (sent.settled) {
                return;
            }
            settled = true;
        }

        data.moveSeq = ++this.moveSeq;
        if (settled) {
            data.settled = true;
        }
        const message: MessageWithRefrId<UpdateMovementMessage> = {
            t: MsgType.UpdateMovement,
            data,
            _refrId
        };
        this.controller.emitter.emit("sendMessageWithRefrId", {
            message,
            reliability: settled ? "reliable" : "unreliable"
        });
        this.lastMovementSent.set(refrIdStr, { key, settled });
        if (_refrId !== undefined) {
            // THORNSWOOD (Thornswood #1287). A hosted reference's place
            // sent while it is still spawning; logs nothing otherwise.
            traceHostedSend(owner.getFormID(), _refrId, data.pos);
        }
    }

    private sendActorValuePercentage(_refrId?: number, form?: FormModel) {
        const canSend = form && (form.isDead ?? false) === false;
        if (!canSend) {
          return;
        }

        const owner = this.getInputOwner(_refrId);
        if (!owner) {
          return;
        }

        const av = getActorValues(this.sp.Game.getPlayer() as Actor);
        const currentTime = Date.now();
        if (
            this.actorValuesNeedUpdate === false &&
            this.prevValues.health === av.health &&
            this.prevValues.stamina === av.stamina &&
            this.prevValues.magicka === av.magicka
        ) {
            return;
        }


        if (
            currentTime - this.prevActorValuesUpdateTime < 2000 &&
            this.actorValuesNeedUpdate === false
        ) {
            return;
        }

        // Delaying actor values update due to casting
        // TODO: use partial updates for actor values once server finally supports it
        // i.e. keep sending health and stamina during casting, but delay magicka update
        if (
            currentTime - this.prevCastingDetectedTime < 500 &&
            av.health > 0 // don't delay death actor value update
        ) {
            return;
        }

        const deathService = this.controller.lookupListener(DeathService);
        if (deathService.isBusy()) {
            logTrace(this, "Not sending actor values, death service is busy");
            return;
        }

        const message: MessageWithRefrId<ChangeValuesMessage> = {
            t: MsgType.ChangeValues,
            data: av,
            _refrId
        };
        this.controller.emitter.emit("sendMessageWithRefrId", {
            message,
            reliability: "unreliable"
        });
        this.actorValuesNeedUpdate = false;
        this.prevValues = av;
        this.prevActorValuesUpdateTime = currentTime;

    }

    private sendAnimation(_refrId?: number) {
        const owner = this.getInputOwner(_refrId);
        if (!owner) {
          return;
        }

        // Extermly important that it's a local id since AnimationSource depends on it
        const refrIdStr = owner.getFormID().toString(16);

        let animSource = this.playerAnimSource.get(refrIdStr);
        if (!animSource) {
            animSource = new AnimationSource(owner);
            this.playerAnimSource.set(refrIdStr, animSource);
        }
        const anim = animSource.getAnimation();

        const lastAnimationSent = this.lastAnimationSent.get(refrIdStr);
        if (
            !lastAnimationSent ||
            anim.numChanges !== lastAnimationSent.numChanges
        ) {
            // Drink potion anim from this mod https://www.nexusmods.com/skyrimspecialedition/mods/97660
            if (anim.animEventName !== '' && !anim.animEventName.startsWith("DrinkPotion_")) {
                this.lastAnimationSent.set(refrIdStr, anim);
                this.updateActorValuesAfterAnimation(anim.animEventName);
                const message: MessageWithRefrId<UpdateAnimationMessage> = {
                    t: MsgType.UpdateAnimation,
                    data: anim,
                    _refrId
                };
                this.controller.emitter.emit("sendMessageWithRefrId", {
                    message,
                    reliability: "unreliable"
                });
            }
        }
    }

    private sendAppearance(_refrId?: number) {
        if (_refrId) {
          return;
        }
        const shown = this.sp.Ui.isMenuOpen('RaceSex Menu');
        if (shown != this.isRaceSexMenuShown) {
            this.isRaceSexMenuShown = shown;
            if (!shown) {
                this.sp.printConsole('Exited from race menu');

                const appearance = getAppearance(this.sp.Game.getPlayer() as Actor);
                // TODO: log appearance contents to debug appearance issues?
                const message: MessageWithRefrId<UpdateAppearanceMessage> = {
                    t: MsgType.UpdateAppearance,
                    data: appearance,
                    _refrId
                };
                this.controller.emitter.emit("sendMessageWithRefrId", {
                    message,
                    reliability: "reliable"
                });
            }
        }
    }

    private sendEquipment(_refrId?: number) {
        if (_refrId) {
            return;
        }
        // Load/equip callbacks fire while the owner inventory is being rebuilt.
        // Preserve the dirty flag until the complete outfit can be reported.
        if (this.sp.storage['ownerInventorySettling'] === true) { return; }
        if (this.equipmentChanged) {
            this.equipmentChanged = false;

            ++this.numEquipmentChanges;

            const eq = getEquipment(
                this.sp.Game.getPlayer() as Actor,
                this.numEquipmentChanges,
            );
            const message: MessageWithRefrId<UpdateEquipmentMessage> = {
                t: MsgType.UpdateEquipment,
                data: eq,
                _refrId
            };

            this.controller.emitter.emit("sendMessageWithRefrId", {
                message,
                reliability: "reliable"
            });
            // THORNSWOOD #6746 measurement: the owner's half of one outfit
            // change. The server's OnUpdateEquipment line and each viewer's
            // "Equipment" lines are the other two.
            logTrace(this, `Equipment sent change ${eq.numChanges}, worn ${wornList(eq)}`);
        }
    }

    private sendHostAttempts() {
        const remoteId = nextHostAttempt();
        if (!remoteId) {
          return;
        }

        this.controller.emitter.emit("sendMessage", {
            message: {
                t: MsgType.Host,
                remoteId
            },
            // THORNSWOOD (Thornswood #6247). Asked once each time the NPC is
            // freed (FormView.tryHostIfNeed), so the ask has to arrive.
            reliability: "reliable"
        });
    }

    private getInputOwner(_refrId?: number) {
        return _refrId
            ? this.sp.Actor.from(this.sp.Game.getFormEx(worldViewMisc.remoteIdToLocalId(_refrId)))
            : this.sp.Game.getPlayer();
    }

    private getForm(refrId: number | undefined, world: WorldModel): FormModel | undefined {
        const form = refrId
            ? world?.forms.find((f) => f?.refrId === refrId)
            : world.forms[world.playerCharacterFormIdx];
        return form;
    }

    private updateActorValuesAfterAnimation(animName: string) {
        if (
            animName === 'JumpLand' ||
            animName === 'JumpLandDirectional' ||
            animName === 'DeathAnim'
        ) {
            this.actorValuesNeedUpdate = true;
        }
    }

    private get singlePlayerService() {
        return this.controller.lookupListener(SinglePlayerService);
    }

    private lastSendMovementMoment = new Map<string, number>();
    private lastMovementSent = new Map<string, { key: string, settled: boolean }>();
    private moveSeq = 0;
    private playerAnimSource = new Map<string, AnimationSource>(); // TODO: make service
    private lastAnimationSent = new Map<string, Animation>();
    private actorValuesNeedUpdate = false;
    private isRaceSexMenuShown = false;
    private equipmentChanged = false;
    private numEquipmentChanges = 0;
    private prevValues: ActorValues = { health: 0, stamina: 0, magicka: 0 };
    private prevActorValuesUpdateTime = 0;
    private prevCastingDetectedTime = 0;
}

// THORNSWOOD (Thornswood #6247). What a move looks like to anybody watching:
// two moves with the same key show the same thing. See sendMovement.
const movementKey = (m: UpdateMovementMessage["data"], idx: number | undefined): string => {
    const r = Math.round;
    return [
        idx, m.worldOrCell,
        r(m.pos[0]), r(m.pos[1]), r(m.pos[2]),
        r(m.rot[0]), r(m.rot[1]), r(m.rot[2]),
        m.runMode, r(m.direction), r(m.speed), r(m.healthPercentage * 100),
        m.isInJumpState, m.isSneaking, m.isBlocking, m.isWeapDrawn, m.isDead,
        m.lookAt ? m.lookAt.map(r).join(",") : "",
        m.teleportSeq ?? "",
    ].join("|");
};
