import { Flora, Form, FormType, MotionType, ObjectReference } from "skyrimPlatform";
import * as sp from "skyrimPlatform";
import { NiPoint3 } from "../sync/movement";
import { FormTypeEx } from "./formTypeEx";

export class ObjectReferenceEx {
  static getWorldOrCell(self: ObjectReference): number {
    let world = self.getWorldSpace();
    if (world) {
      return world.getFormID();
    }

    let cell = self.getParentCell();
    if (cell) {
      return cell.getFormID();
    }

    return 0;
  }

  static getPos(self: ObjectReference): NiPoint3 {
    return [self.getPositionX(), self.getPositionY(), self.getPositionZ()];
  };

  static getDistance(a: NiPoint3, b: NiPoint3) {
    const deltaX = a[0] - b[0];
    const deltaY = a[1] - b[1];
    const deltaZ = a[2] - b[2];
    return Math.sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
  };

  static getDistanceNoZ(a: NiPoint3, b: NiPoint3) {
    const deltaX = a[0] - b[0];
    const deltaY = a[1] - b[1];
    return Math.sqrt(deltaX * deltaX + deltaY * deltaY);
  };

  /*
    THORNSWOOD PATCH (Thornswood #4598). THE BASES THE SERVER ANSWERS FIRST.

    dealWithRef below chooses by record type, and the gamemode answers some
    bases that no type rule names: a chopping block is furniture that is no
    bench, an ore vein is an activator. Left unblocked, the presser's own
    Skyrim sat them at the block or started the swing before the server had
    said whether they may, and the gamemode could only end it afterwards.

    So the server names those bases (the customPacket "serverFirstBases",
    activationService), and every reference of one of them is blocked the way
    a bench is: a refused press changes nothing on this machine, and an
    allowed one comes back as the server's activation.

    The list can arrive after references have streamed, and a later list can
    drop a base, so each furniture and activator reference dealt with is
    remembered and dealt with again when a list arrives. Nothing else is
    remembered: no other type is decided by the list.
  */
  private static serverFirstBases = new Set<number>();
  private static listedRefs = new Set<number>();

  static resetServerFirstActivation(): void {
    // Restore the ordinary rules on references that are still live before
    // forgetting which IDs belonged to the old connection.
    ObjectReferenceEx.setServerFirstBases([]);
    ObjectReferenceEx.listedRefs.clear();
  }

  static setServerFirstBases(baseIds: number[]): void {
    ObjectReferenceEx.serverFirstBases = new Set(baseIds);
    ObjectReferenceEx.listedRefs.forEach((refrId) => {
      const refr = sp.ObjectReference.from(sp.Game.getFormEx(refrId));
      const base = refr ? refr.getBaseObject() : null;
      if (!refr || !base) {
        ObjectReferenceEx.listedRefs.delete(refrId);
        return;
      }
      ObjectReferenceEx.dealWithRef(refr, base);
    });
  }

  static dealWithRef(self: ObjectReference, base: Form): void {
    const t = base.getType();
    const isItem = FormTypeEx.isItem(t);

    // BlackFallsBarrow02, door isn't opening via SetOpen so we're hacking it.
    // Not blocking activation & asking parent to activate until will be in the correct state
    // See also modelApplyUtils.ts
    const caveGSecretDoor01 = 0x6f703;

    /*
      THORNSWOOD PATCH. A locked door or chest keeps Skyrim's own handling
      until it is open.

      Upstream unlocked every tracked reference and blocked activation on it,
      and locked plus blocked is unopenable by any means, so every lock in the
      world was simply gone. The unlock is removed and a locked reference is
      left unblocked, which is what makes the lockpicking mini game appear.

      Activators are not blocked either: levers, chains, bars and shrines
      are Skyrim's to run and the server has no scripts for them. Furniture,
      containers, doors, items and actors go through the server.

      activationService has the other half: while a reference is locked it is
      not announced to the server at all.

      THORNSWOOD PATCH (Thornswood #3080). A plant or a tree goes through the
      server too, before Skyrim picks it.

      Upstream left Flora and Tree unblocked ("I don't think it's
      necessary"). Unblocked, the presser's own Skyrim played the pick,
      emptied the plant and put the produce in its own pack with the game's
      "added" line before the server had said anything. When the server said
      no, the gamemode had to take the produce back out and tell this client
      to un-pick the plant (putBack, on a four second timer). Blocked, a press
      only sends the activation: a refused one changes nothing on this
      machine, and the server hands over what an allowed one gives.

      THORNSWOOD PATCH (Thornswood #6621). All furniture waits for the server.
      Crafting benches already did (#3080), but an ordinary seat processed
      the physical press locally and then processed the server's FURN answer
      again. ActivationService announces both kinds; the server owns their
      occupancy and returns one allowed default activation. Blocking the
      initial local press gives seats that same single path. A refused press
      changes nothing, and ordinary beds, chairs and crafting benches each
      process an accepted press once.

      THORNSWOOD PATCH (Thornswood #4598). And any base the server names as
      one it answers first (setServerFirstBases): a chopping block, an ore
      vein. A locked reference still comes first, as above.
    */
    const listed = t === FormType.Furniture || t === FormType.Activator;
    if (listed) {
      ObjectReferenceEx.listedRefs.add(self.getFormID());
    }
    if (self.isLocked()) {
      self.blockActivation(false);
    } else if (t === FormType.Container
      || isItem
      || t === FormType.NPC
      || t === FormType.Flora
      || t === FormType.Tree
      || t === FormType.Furniture
      || (listed && ObjectReferenceEx.serverFirstBases.has(base.getFormID()))
      || (t === FormType.Door && self.getBaseObject()?.getFormID() !== caveGSecretDoor01)) {
      self.blockActivation(true);
    } else {
      self.blockActivation(false);
    }

    if (isItem) {
      self.setMotionType(MotionType.Keyframed, false);
    }

    // https://github.com/skyrim-multiplayer/issue-tracker/issues/36
    if (t === FormType.Flora && Flora.from(base)?.getIngredient()) {
      self.setMotionType(MotionType.Keyframed, false);
    }
  }
}
