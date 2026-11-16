#include "TestUtils.hpp"

extern espm::Loader& GetEspmLoader();

TEST_CASE("Hypothesis: UpdateMovement may send nothing when actor without "
          "user present",
          "[PartOne]")
{

  PartOne partOne;

  constexpr uint32_t n = 20;
  static_assert(n <= MAX_PLAYERS - 1);

  for (uint32_t i = 0; i < n; ++i) {
    partOne.CreateActor(i + 0xff000000, { 1.f, 2.f, 3.f }, 180.f, 0x3c);
    if (i % 2 == 0)
      continue;

    DoConnect(partOne, i + 1);
    GiveStoredAppearance(
      partOne.worldState.GetFormAt<MpActor>(i + 0xff000000));
    partOne.SetUserActor(i + 1, i + 0xff000000);

    DoUpdateMovement(partOne, i + 0xff000000, i + 1);
  }

  DoConnect(partOne, 0);
  partOne.CreateActor(0xffffffff, { 1.f, 2.f, 3.f }, 180.f, 0x3c);
  GiveStoredAppearance(partOne.worldState.GetFormAt<MpActor>(0xffffffff));
  partOne.SetUserActor(0, 0xffffffff);
  partOne.Messages().clear();

  DoUpdateMovement(partOne, 0xffffffff, 0);
  // The 10 other users created in the loop; never back to the sender
  // (Thornswood #6251).
  REQUIRE(partOne.Messages().size() == 10);
}

TEST_CASE("UpdateMovement when neighbour has been disconnected", "[PartOne]")
{

  PartOne partOne;

  for (int i = 0; i < 2; ++i) {
    DoConnect(partOne, i);
    partOne.CreateActor(i + 0xff000ABC, { 1.f, 2.f, 3.f }, 180.f, 0x3c);
    GiveStoredAppearance(
      partOne.worldState.GetFormAt<MpActor>(i + 0xff000ABC));
    partOne.SetUserActor(i, i + 0xff000ABC);
    auto m = jMovement;
    m["idx"] = i;
    DoMessage(partOne, i, m);
  }

  partOne.Messages().clear();

  DoDisconnect(partOne, 1);

  partOne.Messages().clear();
  DoMessage(partOne, 0, jMovement);
  // Nobody left to tell: a move never goes back to its sender (#6251).
  REQUIRE(partOne.Messages().empty());
}

TEST_CASE("UpdateMovement", "[PartOne]")
{

  PartOne partOne;

  // Passed on numbered by the server, one more for each move of this actor
  // (Thornswood #6247).
  auto relayed = [](uint32_t moveSeq) {
    auto j = jMovement;
    j["data"]["moveSeq"] = moveSeq;
    return j;
  };

  auto doMovement = [&] { DoMessage(partOne, 0, jMovement); };

  DoConnect(partOne, 0);

  doMovement();
  REQUIRE(partOne.Messages().size() == 0); // No actor - no movement

  partOne.CreateActor(0xff000ABC, { 1.f, 2.f, 3.f }, 180.f, 0x3c);
  GiveStoredAppearance(partOne.worldState.GetFormAt<MpActor>(0xff000ABC));
  partOne.SetUserActor(0, 0xff000ABC);
  partOne.Messages().clear();
  doMovement();
  // Nobody else sees it yet, and a move never goes back to its sender
  // (Thornswood #6251).
  REQUIRE(partOne.Messages().empty());

  // UpdateMovement actually changes position and rotation
  REQUIRE(
    dynamic_cast<MpActor*>(partOne.worldState.LookupFormById(0xff000ABC).get())
      ->GetPos() == NiPoint3{ 1, -1, 1 });
  REQUIRE(
    dynamic_cast<MpActor*>(partOne.worldState.LookupFormById(0xff000ABC).get())
      ->GetAngle() == NiPoint3{ 0, 0, 179 });

  // Another player connects and see us
  DoConnect(partOne, 1);
  partOne.CreateActor(0xff00ABCD, { 1.f, 2.f, 3.f }, 180.f, 0x3c);
  GiveStoredAppearance(partOne.worldState.GetFormAt<MpActor>(0xff00ABCD));
  partOne.Messages().clear();
  partOne.SetUserActor(1, 0xff00ABCD);
  REQUIRE(partOne.Messages().size() == 3);
  // Create idx 1 for user 0, then idx 0 for 1, then idx 1 for 1 (self
  // streaming)
  REQUIRE(std::find_if(partOne.Messages().begin(), partOne.Messages().end(),
                       [&](auto m) {
                         return m.j["t"] == MsgType::CreateActor &&
                           m.j["idx"] == 1 && m.reliable && m.userId == 0;
                       }) != partOne.Messages().end());
  REQUIRE(std::find_if(partOne.Messages().begin(), partOne.Messages().end(),
                       [&](auto m) {
                         return m.j["t"] == MsgType::CreateActor &&
                           m.j["idx"] == 0 && m.reliable && m.userId == 1;
                       }) != partOne.Messages().end());
  REQUIRE(std::find_if(partOne.Messages().begin(), partOne.Messages().end(),
                       [&](auto m) {
                         return m.j["t"] == MsgType::CreateActor &&
                           m.j["idx"] == 1 && m.reliable && m.userId == 1;
                       }) != partOne.Messages().end());

  partOne.Messages().clear();
  doMovement();
  REQUIRE(partOne.Messages().size() == 1);
  REQUIRE(partOne.Messages().at(0).userId == 1);
  REQUIRE(partOne.Messages().at(0).j == relayed(2));

  // Another player is being moved away and now doesn't see our movement
  auto acAbcd = dynamic_cast<MpActor*>(
    partOne.worldState.LookupFormById(0xff00ABCD).get());
  partOne.Messages().clear();
  acAbcd->SetPos({ 100000, 0, 0 });
  REQUIRE(partOne.Messages().size() == 2);
  REQUIRE(std::find_if(partOne.Messages().begin(), partOne.Messages().end(),
                       [&](auto m) {
                         return m.j["t"] == MsgType::DestroyActor &&
                           m.j["idx"] == 1 && m.reliable && m.userId == 0;
                       }) != partOne.Messages().end());
  REQUIRE(std::find_if(partOne.Messages().begin(), partOne.Messages().end(),
                       [&](auto m) {
                         return m.j["t"] == MsgType::DestroyActor &&
                           m.j["idx"] == 0 && m.reliable && m.userId == 1;
                       }) != partOne.Messages().end());
}

// Thornswood #6247. Moves are sent when they change, so the newest one has
// to stick: one overtaken on the way is dropped, and the one a client sends
// once nothing changes any more goes on reliably.
TEST_CASE("A move overtaken on the way is neither applied nor passed on",
          "[PartOne]")
{
  PartOne partOne;
  DoConnect(partOne, 0);
  partOne.CreateActor(0xff000ABC, { 1.f, 2.f, 3.f }, 180.f, 0x3c);
  GiveStoredAppearance(partOne.worldState.GetFormAt<MpActor>(0xff000ABC));
  partOne.SetUserActor(0, 0xff000ABC);
  auto& actor = partOne.worldState.GetFormAt<MpActor>(0xff000ABC);
  // Somebody standing beside them, who is told their moves.
  DoConnect(partOne, 1);
  partOne.CreateActor(0xff00ABCD, { 1.f, 2.f, 3.f }, 180.f, 0x3c);
  GiveStoredAppearance(partOne.worldState.GetFormAt<MpActor>(0xff00ABCD));
  partOne.SetUserActor(1, 0xff00ABCD);

  auto move = [&](uint32_t seq, NiPoint3 pos, bool settled) {
    auto j = jMovement;
    j["data"]["moveSeq"] = seq;
    j["data"]["pos"] = { pos.x, pos.y, pos.z };
    if (settled) {
      j["data"]["settled"] = true;
    }
    partOne.Messages().clear();
    DoMessage(partOne, 0, j);
  };

  move(5, { 1, -1, 1 }, false);
  REQUIRE(partOne.Messages().size() == 1);
  REQUIRE_FALSE(partOne.Messages()[0].reliable);
  REQUIRE(partOne.Messages()[0].j["data"]["moveSeq"] == 1);
  REQUIRE(actor.GetPos() == NiPoint3{ 1, -1, 1 });

  move(4, { 5, 5, 5 }, false);
  REQUIRE(partOne.Messages().empty());
  REQUIRE(actor.GetPos() == NiPoint3{ 1, -1, 1 });

  move(5, { 5, 5, 5 }, true);
  REQUIRE(partOne.Messages().empty());

  move(6, { 2, 2, 2 }, true);
  REQUIRE(partOne.Messages().size() == 1);
  REQUIRE(partOne.Messages()[0].reliable);
  REQUIRE(partOne.Messages()[0].j["data"]["moveSeq"] == 2);
  REQUIRE(partOne.Messages()[0].j["data"]["settled"] == true);
  REQUIRE(actor.GetPos() == NiPoint3{ 2, 2, 2 });

  // A new connection to the same character counts from 1 again.
  partOne.SetUserActor(0, 0xff000ABC);
  move(1, { 3, 3, 3 }, false);
  REQUIRE(partOne.Messages().size() == 1);
  REQUIRE(partOne.Messages()[0].userId == 1);
  REQUIRE(partOne.Messages()[0].j["data"]["moveSeq"] == 3);
  REQUIRE(actor.GetPos() == NiPoint3{ 3, 3, 3 });
}

TEST_CASE("A property set to the value it holds tells nobody", "[PartOne]")
{
  // A property message names the base record's type, so it reads the espm.
  PartOne partOne;
  partOne.AttachEspm(&GetEspmLoader());
  DoConnect(partOne, 0);
  partOne.CreateActor(0xff000ABC, { 1.f, 2.f, 3.f }, 180.f, 0x3c);
  GiveStoredAppearance(partOne.worldState.GetFormAt<MpActor>(0xff000ABC));
  partOne.SetUserActor(0, 0xff000ABC);
  auto& actor = partOne.worldState.GetFormAt<MpActor>(0xff000ABC);

  partOne.Messages().clear();
  actor.SetPropertyValueDump("thornswoodTest", "{\"a\":1}", true, true);
  REQUIRE(partOne.Messages().size() == 1);

  partOne.Messages().clear();
  actor.SetPropertyValueDump("thornswoodTest", "{\"a\":1}", true, true);
  REQUIRE(partOne.Messages().empty());

  actor.SetPropertyValueDump("thornswoodTest", "{\"a\":2}", true, true);
  REQUIRE(partOne.Messages().size() == 1);
  REQUIRE(actor.GetDynamicFields().GetValueDump("thornswoodTest") ==
          "{\"a\":2}");
}
