#pragma once
#include "Inventory.h"
#include "NetworkingInterface.h"
#include "libespm/Loader.h"
#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>

class PartOne;
struct RawMessageData;
class MpActor;

class CraftService
{
public:
  explicit CraftService(PartOne& partOne_);

  void OnCraftItem(
    const RawMessageData& rawMsgData, const Inventory& inputObjects,
    uint32_t workbenchId, uint32_t resultObjectId,
    const std::optional<Inventory::Made>& resultPotion = std::nullopt,
    const std::optional<Inventory::MadeEnchantment>& resultEnchantment =
      std::nullopt,
    std::optional<float> resultMaxCharge = std::nullopt);

  // THORNSWOOD PATCH (Thornswood #3161). The person left the bench: its
  // menu closed, which is getting up from it. Every craft still being worked
  // for them is stopped, and nothing it would have taken is taken. The
  // client says so with the custom packet { "customPacketType": "craftLeave" }
  // (ActionListener::OnCustomPacket).
  void StopWork(Networking::UserId userId);

  // public for CraftTest.cpp: how many crafts are being worked or wait to be
  // for this actor.
  size_t GetWorkCount(uint32_t actorId) const;

  // public for CraftTest.cpp. How far from the bench, in game units, a
  // person may be when a craft's time is up. Further away they have moved
  // away, and the work stops (Patrick, 22 Oct 2026: "moving away stops it").
  // Somebody working a bench stands at its furniture marker, an arm's length
  // or two from the reference; this is several strides beyond that.
  static constexpr float kWorkReach = 512.f;

  // public for CraftTest.cpp
  bool RecipeItemsMatch(const espm::LookupResult& lookupRes,
                        const Inventory& inputObjects,
                        uint32_t resultObjectId);

  // public for CraftTest.cpp
  std::vector<espm::LookupResult> FindRecipe(
    std::optional<MpActor*> me,
    std::optional<std::vector<uint32_t>> workbenchKeywordIds,
    const espm::CombineBrowser& br, const Inventory& inputObjects,
    uint32_t resultObjectId);

  // THORNSWOOD PATCH (Thornswood #1389). The magic effects a brew of these
  // inputs holds, worked out from the ingredient records the way the game
  // works them out in the alchemy menu: every MGEF that at least two of the
  // distinct ingredients carry, as global form ids, in the order the inputs
  // first name them. nullopt when no input is an ingredient (an enchantment,
  // or anything else with no COBJ), which onCraft passes on as "not named".
  // public for CraftTest.cpp
  std::optional<std::vector<uint32_t>> BrewEffects(
    const espm::CombineBrowser& br, const Inventory& inputObjects);

  // THORNSWOOD PATCH (Thornswood #1649). What the server keeps of a brewed
  // bottle, or nullopt when the brew is not believable and is refused.
  //
  // `effects` is BrewEffects of the inputs, the server's own answer to which
  // effects are in it. `claimed` is what the client read off the bottle the
  // game made. The client's list has to name exactly those effects, each
  // once (the game orders them by cost, so the order may differ and is kept
  // as the client gives it: the first is the one the game names the bottle
  // after). What only the game knows, how strong and how long each one came
  // out after the person's skill and perks, is taken from the client, held
  // to at most kMadeScaleLimit times the strongest ingredient's own value
  // for that effect, which is far above anything the alchemy perks and
  // fortify gear reach, and only stops a number nothing could have brewed.
  // The result is rounded (Inventory::Round), so its id is stable.
  // public for CraftTest.cpp
  static constexpr float kMadeScaleLimit = 64.f;
  std::optional<Inventory::Made> MadePotion(
    const espm::CombineBrowser& br, const Inventory& inputObjects,
    const std::vector<uint32_t>& effects,
    const std::optional<Inventory::Made>& claimed);

  // THORNSWOOD PATCH (Thornswood #4513). What the client read off what a
  // craft gave: a brewed bottle's contents (#1649), or the enchantment the
  // game put on a piece and the charge the piece holds at most.
  struct Claimed
  {
    std::optional<Inventory::Made> potion;
    std::optional<Inventory::MadeEnchantment> enchantment;
    std::optional<float> maxCharge;
  };

  // THORNSWOOD PATCH (Thornswood #4513). What the server keeps of an
  // enchantment made on a piece, or nullopt when it is not believable and
  // the craft is refused.
  //
  // There is no record to work the effects out from, as a brew's
  // ingredients are for a bottle: which enchantments a person may put on a
  // piece is the gamemode's list (docs/systems/enchantments.md). What is
  // checked here is that it is an enchantment of that piece at all: the
  // piece is a weapon or armour record and one of the inputs, a soul gem is
  // another, the enchantment says the same kind of piece, it holds one or
  // two effects (two with Twin Souls), each a magic effect record named
  // once, and every number is one the game could give: finite, at most
  // kEnchantMagnitudeLimit, an area and a duration whole and at most
  // kEnchantDurationLimit, a weapon's charge at most kMaxChargeLimit (a
  // grand or black soul is 3000) and armour's none. The result is rounded
  // (Inventory::Round), so its id is stable.
  // public for MadeEnchantmentTest.cpp
  static constexpr float kEnchantMagnitudeLimit = 1000.f;
  static constexpr uint32_t kEnchantDurationLimit = 3600;
  static constexpr float kMaxChargeLimit = 3000.f;
  std::optional<Inventory::MadeEnchantment> MadeEnchantment(
    const espm::CombineBrowser& br, const Inventory& inputObjects,
    uint32_t resultObjectId, const Claimed& claimed);

private:
  // THORNSWOOD PATCH (Thornswood #3161). What a craft is, worked out from
  // what the client says went in and came out: the entries it takes, what
  // it gives, its recipe (0 for none) and a brew's effects. Worked out the
  // same way when the craft is ordered and when its time is up.
  struct Craft
  {
    std::vector<Inventory::Entry> entries;
    uint32_t outputBaseId = 0;
    uint32_t outputCount = 0;
    uint32_t recipeId = 0;
    std::optional<std::vector<uint32_t>> effects;
    // Thornswood #1649. A brew's bottle by its contents (MadePotion), and
    // then outputBaseId is Inventory::MadeBaseId of it.
    std::optional<Inventory::Made> made;
    // Thornswood #4513. An enchanted piece's enchantment by its contents
    // (MadeEnchantment), and the charge the piece holds at most.
    std::optional<Inventory::MadeEnchantment> enchantment;
    std::optional<float> maxCharge;
  };

  // What a craft that went through gave, which the client is told so it can
  // put back the item it hid when the person pressed craft. Thornswood #1649:
  // a brewed bottle's contents go with it, since its base id is a made id the
  // client has to build (ensureMadePotion) before it can add it. Thornswood
  // #4513: so does a made enchantment, and the piece goes back with it.
  struct Made
  {
    uint32_t baseId = 0;
    uint32_t count = 0;
    std::optional<Inventory::Made> contents;
    std::optional<Inventory::MadeEnchantment> enchantment;
    std::optional<float> maxCharge;
  };

  // A craft that is being worked or waits its turn, oldest first.
  struct Order
  {
    uint64_t id = 0;
    Networking::UserId userId = Networking::InvalidUserId;
    Inventory inputObjects;
    uint32_t workbenchId = 0;
    uint32_t resultObjectId = 0;
    // Thornswood #1649, #4513: what the client read off what the craft
    // gave, worked out again when the time is up, the same as the inputs.
    Claimed claimed;
    std::chrono::milliseconds time{ 0 };
    // What the craft will take, held back from every later order of the
    // same person so two orders cannot both count the same ingot.
    std::vector<Inventory::Entry> takes;
  };

  // nullopt when this is no craft the server would carry out: not a bench,
  // a recipe that is not usable here, or a brew without a believable
  // description of its bottle (Thornswood #1649), or an enchantment that is
  // not believable (Thornswood #4513). Logs why.
  std::optional<Craft> WorkOut(MpActor* me, const Inventory& inputObjects,
                               uint32_t workbenchId, uint32_t resultObjectId,
                               const Claimed& claimed);

  // THORNSWOOD PATCH. The craft itself, split out of OnCraftItem so that
  // every way it can fail ends in the same place. True when it went through,
  // false when it did not; it can also throw. See OnCraftItem. `made` is
  // what it gave, when it went through.
  bool CraftItem(MpActor* me, const Inventory& inputObjects,
                 uint32_t workbenchId, uint32_t resultObjectId,
                 const Claimed& claimed, Made* made = nullptr);

  // THORNSWOOD PATCH (Thornswood #3161). Asks onCraftWork how long the
  // craft takes. nullopt when it is refused before any time passes; the
  // reason is logged. `takes` is set to what the craft will take.
  std::optional<std::chrono::milliseconds> AskWorkTime(
    MpActor* me, const Inventory& inputObjects, uint32_t workbenchId,
    uint32_t resultObjectId, const Claimed& claimed,
    std::vector<Inventory::Entry>& takes);

  void StartNextOrder(uint32_t actorId);
  void FinishOrder(uint32_t actorId, uint64_t orderId);
  bool AtBench(const MpActor& me, uint32_t workbenchId);
  void SendMade(Networking::UserId userId, const Made& made);

  // THORNSWOOD PATCH. Sends the actor's own client the inventory the server
  // holds, after a craft that did not go through. See OnCraftItem.
  void SendInventoryBack(MpActor* me, uint32_t workbenchId,
                         uint32_t resultObjectId, const std::string& why);

  bool ConsiderRecipeCandidate(
    std::optional<MpActor*> me,
    std::optional<std::vector<uint32_t>> workbenchKeywordIds,
    const espm::LookupResult& lookupRes);

  // THORNSWOOD PATCH (Thornswood #3161): what a recipe takes and gives,
  // for WorkOut. Was UseCraftRecipe, which also asked onCraft; CraftItem
  // asks it now, for a recipe and for none alike.
  Craft CraftFromRecipe(MpActor* me, const espm::COBJ* recipeUsed,
                        espm::CompressedFieldsCache& cache,
                        const espm::CombineBrowser& br, int espmIdx);

  bool EvaluateCraftRecipeConditions(MpActor* me,
                                     const espm::COBJ::Data& recipeData);

  PartOne& partOne;
  // THORNSWOOD PATCH (Thornswood #3161). The crafts being worked, by actor.
  // The front order of each is the one whose time is running.
  std::map<uint32_t, std::deque<Order>> work;
  uint64_t nextOrderId = 1;
  std::vector<espm::LookupResult> allRecipes;
  espm::CompressedFieldsCache cache;
};
