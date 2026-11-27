#include "TestUtils.hpp"

using Catch::Matchers::ContainsSubstring;

TEST_CASE("CreateActor/DestroyActor", "[PartOne]")
{

  PartOne partOne;

  // Create:

  REQUIRE(!partOne.worldState.LookupFormById(0xff000ABC));
  partOne.CreateActor(0xff000ABC, { 1.f, 2.f, 3.f }, 180.f, 0x3c);

  auto form = partOne.worldState.LookupFormById(0xff000ABC).get();
  REQUIRE(form);
  REQUIRE(form->GetFormId() == 0xff000ABC);

  auto ac = dynamic_cast<MpActor*>(form);
  REQUIRE(ac);
  REQUIRE(ac->GetPos() == NiPoint3{ 1.f, 2.f, 3.f });
  REQUIRE(ac->GetAngle() == NiPoint3{ 0.f, 0.f, 180.f });
  REQUIRE(ac->GetCellOrWorld() == FormDesc::Tamriel());

  // Destroy:

  partOne.DestroyActor(0xff000ABC);
  REQUIRE(!partOne.worldState.LookupFormById(0xff000ABC));
}

TEST_CASE("SetUserActor", "[PartOne]")
{

  PartOne partOne;
  partOne.CreateActor(0xff000ABC, { 1.f, 2.f, 3.f }, 180.f, 0x3c);
  DoConnect(partOne, 0);

  REQUIRE(!partOne.GetUserActor(0));
  partOne.SetUserActor(0, 0xff000ABC);
  REQUIRE(partOne.GetUserActor(0) == 0xff000ABC);
  REQUIRE(partOne.Messages().size() == 1);
  REQUIRE(partOne.Messages().at(0).message);

  auto createActorMessage =
    dynamic_cast<CreateActorMessage*>(partOne.Messages().at(0).message.get());

  REQUIRE(createActorMessage);
  REQUIRE(createActorMessage->refrId == 0xff000ABC);
  REQUIRE(createActorMessage->idx == 0);
  REQUIRE(createActorMessage->customPropsJsonDumps.empty());
  REQUIRE(createActorMessage->isMe == true);
  REQUIRE(createActorMessage->props.healRate == 0.7f);
  REQUIRE(createActorMessage->props.healRateMult == 100.f);
  REQUIRE(createActorMessage->props.health == 100.f);
  REQUIRE(createActorMessage->props.isHostedByOther == true);
  REQUIRE(createActorMessage->props.learnedSpells == std::vector<uint32_t>());
  REQUIRE(createActorMessage->props.magicka == 100.f);
  REQUIRE(createActorMessage->props.magickaRate == 3.f);
  REQUIRE(createActorMessage->props.magickaRateMult == 100.f);
  REQUIRE(createActorMessage->props.stamina == 100.f);
  REQUIRE(createActorMessage->props.staminaRate == 5.f);
  REQUIRE(createActorMessage->props.staminaRateMult == 100.f);
  REQUIRE(createActorMessage->props.healthPercentage == 1.f);
  REQUIRE(createActorMessage->props.staminaPercentage == 1.f);
  REQUIRE(createActorMessage->props.magickaPercentage == 1.f);
  REQUIRE(createActorMessage->transform.pos ==
          std::array<float, 3>{ 1.f, 2.f, 3.f });
  REQUIRE(createActorMessage->transform.rot ==
          std::array<float, 3>{ 0.f, 0.f, 180.f });
  REQUIRE(createActorMessage->transform.worldOrCell == 0x3c);

  // Trying to destroy actor:
  partOne.DestroyActor(0xff000ABC);
  REQUIRE(!partOne.GetUserActor(0));

  // TODO: More manipulations with actor transfer
}

TEST_CASE("Use SetUserActor with 0 formId argument", "[PartOne]")
{

  PartOne partOne;
  DoConnect(partOne, 1);
  partOne.CreateActor(0xff000ABC, { 1.f, 2.f, 3.f }, 180.f, 0x3c);

  REQUIRE(partOne.GetUserActor(1) == 0);
  partOne.SetUserActor(1, 0xff000ABC);
  REQUIRE(partOne.GetUserActor(1) == 0xff000ABC);
  partOne.SetUserActor(1, 0);
  REQUIRE(partOne.GetUserActor(1) == 0);
}

TEST_CASE("SetUserActor failures", "[PartOne]")
{
  PartOne partOne;
  REQUIRE_THROWS_WITH(partOne.SetUserActor(9, 0xff000000),
                      ContainsSubstring("User with id 9 doesn't exist"));
  DoConnect(partOne, 9);

  REQUIRE_THROWS_WITH(
    partOne.SetUserActor(9, 0xff000000),
    ContainsSubstring("Form with id 0xff000000 doesn't exist"));

  partOne.worldState.AddForm(std::unique_ptr<MpForm>(new MpForm), 0xff000000);

  REQUIRE_THROWS_WITH(
    partOne.SetUserActor(9, 0xff000000),
    ContainsSubstring("Form with id 0xff000000 is not Actor"));
}

TEST_CASE("createActor message contains Appearance", "[PartOne]")
{

  PartOne partOne;

  DoConnect(partOne, 0);
  partOne.CreateActor(0xff000ABC, { 1.f, 2.f, 3.f }, 180.f, 0x3c);
  partOne.SetUserActor(0, 0xff000ABC);
  const Appearance appearance = Appearance::FromJson(jAppearance["data"]);
  partOne.worldState.GetFormAt<MpActor>(0xff000ABC).SetAppearance(&appearance);

  partOne.Messages().clear();

  DoConnect(partOne, 1);
  partOne.CreateActor(0xff000FFF, { 100.f, 200.f, 300.f }, 180.f, 0x3c);
  partOne.SetUserActor(1, 0xff000FFF);

  auto res = FindRefrMessageIdx<CreateActorMessage>(partOne, 0);
  REQUIRE(res.filteredMessages.size() == 1);
  REQUIRE(res.filteredMessages[0].appearance.has_value());
  REQUIRE(*res.filteredMessages[0].appearance == appearance);

  /*REQUIRE_THROWS_WITH(
    doAppearance(), ContainsSubstring("Unable to update appearance, RaceMenu is
  not open"));

  partOne.SetRaceMenuOpen(0xff000ABC, true);
  doAppearance();*/
}

TEST_CASE("GetUserActor", "[PartOne]")
{

  PartOne partOne;

  DoConnect(partOne, 0);
  partOne.CreateActor(0xff000000, { 0, 0, 0 }, 0, 0x3c);
  partOne.SetUserActor(0, 0xff000000);

  auto& ac = partOne.worldState.GetFormAt<MpActor>(0xff000000);

  REQUIRE(partOne.GetUserActor(0) == 0xff000000);
  REQUIRE(partOne.serverState.ActorByUser(0) != nullptr);
  REQUIRE(partOne.serverState.UserByActor(&ac) == 0);

  DoDisconnect(partOne, 0);

  REQUIRE(partOne.serverState.ActorByUser(0) == nullptr);
  REQUIRE(partOne.serverState.UserByActor(&ac) == Networking::InvalidUserId);
  REQUIRE_THROWS_WITH(partOne.GetUserActor(0),
                      ContainsSubstring("User with id 0 doesn't exist"));
}

TEST_CASE("Destroying actor in disconnect event handler", "[PartOne]")
{

  static PartOne partOne;

  DoConnect(partOne, 0);
  partOne.CreateActor(0xff000ABC, { 1.f, 2.f, 3.f }, 180.f, 0x3c);
  partOne.SetUserActor(0, 0xff000ABC);
  DoMessage(partOne, 0, jMovement);

  static auto& ac = partOne.worldState.GetFormAt<MpActor>(0xff000ABC);

  class Listener : public PartOne::Listener
  {
  public:
    void OnConnect(Networking::UserId userId) override {}
    void OnDisconnect(Networking::UserId userId) override
    {
      REQUIRE(partOne.serverState.UserByActor(&ac) == 0);
      partOne.DestroyActor(0xff000ABC);
    }
    void OnCustomPacket(Networking::UserId userId,
                        const simdjson::dom::element& content) override
    {
    }
    bool OnMpApiEvent(const GameModeEvent&) override { return true; }
  };

  partOne.AddListener(std::shared_ptr<PartOne::Listener>(new Listener));

  REQUIRE(partOne.serverState.UserByActor(&ac) == 0);
  DoDisconnect(partOne, 1);
  REQUIRE(partOne.serverState.UserByActor(&ac) == Networking::InvalidUserId);
}

TEST_CASE("Bug with subscription", "[PartOne]")
{

  PartOne partOne;
  DoConnect(partOne, 0);

  partOne.CreateActor(0xff000000, { 1, 1, 1 }, 3, 0x3c);
  partOne.SetEnabled(0xff000000, true);
  partOne.SetEnabled(0xff000000, false);
  partOne.SetEnabled(0xff000000, true);
  partOne.SetEnabled(0xff000000, false);
  partOne.SetEnabled(0xff000000, true);
  partOne.SetUserActor(0, 0xff000000);

  REQUIRE(partOne.Messages().size() == 1);
  REQUIRE(partOne.Messages()[0].j["t"] == MsgType::CreateActor);
}

TEST_CASE("SetUserActor doesn't work with disabled actors", "[PartOne]")
{
  PartOne partOne;

  REQUIRE_THROWS_WITH(partOne.GetUserActor(Networking::InvalidUserId),
                      ContainsSubstring("User with id 65535 doesn't exist"));

  REQUIRE_THROWS_WITH(partOne.SetUserActor(Networking::InvalidUserId, 0),
                      ContainsSubstring("User with id 65535 doesn't exist"));
}

TEST_CASE("Actor should see its inventory in 'createActor' message",
          "[PartOne]")
{

  PartOne partOne;
  DoConnect(partOne, 0);

  partOne.CreateActor(0xff000000, { 1, 1, 1 }, 3, 0x3c);
  partOne.worldState.GetFormAt<MpActor>(0xff000000).AddItem(0x12eb7, 3);
  partOne.SetUserActor(0, 0xff000000);

  REQUIRE(partOne.Messages().size() == 1);
  REQUIRE(partOne.Messages()[0].j["t"] == MsgType::CreateActor);
  REQUIRE(partOne.Messages()[0].j["props"]["inventory"] ==
          Inventory().AddItem(0x12eb7, 3).ToJson());
}

TEST_CASE(
  "An owner's create packet states its authoritative pack even when empty",
  "[PartOne][OwnerInventory]")
{
  PartOne partOne;
  constexpr uint32_t actorId = 0xff000abc;
  partOne.CreateActor(actorId, { 1, 1, 1 }, 3, 0x3c);
  auto& actor = partOne.worldState.GetFormAt<MpActor>(actorId);
  Inventory pack;
  pack.AddItem(0x12eb7, 3);
  actor.SetInventory(pack);
  Equipment snapshot;
  snapshot.inv =
    pack; // The client's equipment report also contains unworn rows.
  snapshot.numChanges = 9;
  actor.SetEquipment(snapshot);

  bool creation = false;
  SECTION("an emptied pack takes precedence over its retained unworn snapshot")
  {
    pack = Inventory();
    actor.SetInventory(pack);
    REQUIRE(actor.GetEquipment() == snapshot);
  }
  SECTION("a nonempty pack retains its exact contents")
  {
  }
  SECTION("a new empty pack with no old equipment is still explicit")
  {
    pack = Inventory();
    snapshot = Equipment();
    actor.SetInventory(pack);
    actor.SetEquipment(snapshot);
  }
  SECTION("creation receives an explicit empty pack too")
  {
    pack = Inventory();
    actor.SetInventory(pack);
    actor.SetRaceMenuOpen(true);
    creation = true;
  }

  DoConnect(partOne, 0);
  partOne.SetUserActor(0, actorId);
  REQUIRE(partOne.Messages().size() == 1);
  auto& sent = partOne.Messages().front();
  auto* created = dynamic_cast<CreateActorMessage*>(sent.message.get());
  REQUIRE(created);
  REQUIRE(created->isMe);
  REQUIRE(created->props.inventory.has_value());
  REQUIRE(created->props.inventory->ToJson() == pack.ToJson());
  REQUIRE(created->equipment.has_value());
  REQUIRE(*created->equipment == snapshot);
  // PartOne's fake sender decodes the real binary packet before making j.
  REQUIRE(sent.j.at("props").at("inventory") == pack.ToJson());
  if (creation)
    REQUIRE(created->props.isRaceMenuOpen == true);
}

TEST_CASE("An observer's create packet never publishes the private pack",
          "[PartOne][OwnerInventory]")
{
  PartOne partOne;
  constexpr uint32_t ownerId = 0xff000abc;
  constexpr uint32_t viewerId = 0xff000fff;
  partOne.CreateActor(ownerId, { 1, 1, 1 }, 3, 0x3c);
  auto& owner = partOne.worldState.GetFormAt<MpActor>(ownerId);
  GiveStoredAppearance(owner);
  SECTION("empty private pack")
  {
  }
  SECTION("nonempty private pack")
  {
    owner.AddItem(0x12eb7, 3);
  }
  DoConnect(partOne, 0);
  partOne.SetUserActor(0, ownerId);
  DoConnect(partOne, 1);
  partOne.CreateActor(viewerId, { 2, 2, 2 }, 3, 0x3c);
  partOne.Messages().clear();
  partOne.SetUserActor(1, viewerId);

  auto observed =
    FindRefrMessageIdx<CreateActorMessage>(partOne, owner.GetIdx());
  REQUIRE(observed.filteredMessages.size() == 1);
  REQUIRE(observed.filteredMessagesOriginals.front().userId == 1);
  const auto& created = observed.filteredMessages.front();
  REQUIRE_FALSE(created.isMe);
  REQUIRE_FALSE(created.props.inventory.has_value());
  const auto& json = observed.filteredMessagesOriginals.front().j;
  REQUIRE_FALSE(json.at("props").contains("inventory"));
}

TEST_CASE("'isRaceMenuOpen' property should present in 'createActor'",
          "[PartOne]")
{

  PartOne partOne;
  DoConnect(partOne, 0);

  partOne.CreateActor(0xff000000, { 1, 1, 1 }, 3, 0x3c);
  partOne.worldState.GetFormAt<MpActor>(0xff000000).SetRaceMenuOpen(true);
  partOne.SetUserActor(0, 0xff000000);

  REQUIRE(partOne.Messages().size() == 1);
  REQUIRE(partOne.Messages()[0].j["t"] == MsgType::CreateActor);
  REQUIRE(partOne.Messages()[0].j["props"]["isRaceMenuOpen"] == true);
}
