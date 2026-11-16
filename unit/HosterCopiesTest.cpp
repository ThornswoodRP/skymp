#include "ActionListener.h"
#include "HostMessage.h"
#include "HostStartMessage.h"
#include "TestUtils.hpp"
#include "UpdateEquipmentMessage.h"
#include "UpdatePropertyMessage.h"

extern espm::Loader& GetEspmLoader();

// Thornswood #6252. A message for everybody near a form went to every actor
// near it, and an NPC's copy was sent on to the person hosting it, so a
// person hosting k NPCs near the form got it k+1 times. The extra copies
// are about a form that person already hears about as a listener, or about
// one that person's game never loaded. Worse, the hosting news an NPC's copy
// carried was worked out for the NPC, so the hoster of a nearby NPC heard
// "hosted by another" about an NPC it had just taken.
namespace {
size_t CountTo(PartOne& p, Networking::UserId userId,
               const std::function<bool(IMessageBase*)>& what)
{
  size_t n = 0;
  for (const auto& sent : p.Messages()) {
    if (sent.userId == userId && what(sent.message.get())) {
      ++n;
    }
  }
  return n;
}

MpActor& AddNpc(PartOne& p, uint32_t formId)
{
  // A creature base, so it is shown to everybody near it, put on the grid
  // as an NPC loaded from the espm is (AddForm alone leaves it off it).
  p.worldState.AddForm(
    std::make_unique<MpActor>(
      LocationalData{ { 0, 0, 0 }, {}, FormDesc::Tamriel() },
      p.CreateFormCallbacks(), 0x1e7a4),
    formId);
  auto& npc = p.worldState.GetFormAt<MpActor>(formId);
  npc.ForceSubscriptionsUpdate();
  return npc;
}
}

TEST_CASE("A person hosting NPCs gets one copy of what is said near them",
          "[HostAttempt][PartOne]")
{
  PartOne p;
  p.AttachEspm(&GetEspmLoader());
  auto face = jAppearance["data"];
  face["raceId"] = 0x13746; // NordRace, Skyrim.esm
  DoConnect(p, 0);
  p.CreateActor(0xff000000, { 0, 0, 0 }, 0, 0x3c);
  GiveStoredAppearance(p.worldState.GetFormAt<MpActor>(0xff000000), face);
  p.SetUserActor(0, 0xff000000);

  auto& first = AddNpc(p, 0xff000001);
  auto& second = AddNpc(p, 0xff000002);
  auto& third = AddNpc(p, 0xff000003);

  ActionListener listener(p);
  RawMessageData raw;
  raw.userId = 0;
  HostMessage host;
  host.remoteId = second.GetFormId();
  listener.OnHostAttempt(raw, host);
  host.remoteId = third.GetFormId();
  listener.OnHostAttempt(raw, host);
  REQUIRE(p.worldState.hosters.at(second.GetFormId()) == 0xff000000);
  REQUIRE(p.worldState.hosters.at(third.GetFormId()) == 0xff000000);

  SECTION("a property")
  {
    p.Messages().clear();
    first.SetPropertyValueDump("thornswoodTest", "1", true, true);
    REQUIRE(CountTo(p, 0, [&](IMessageBase* m) {
              auto prop = dynamic_cast<UpdatePropertyMessage*>(m);
              return prop && prop->idx == first.GetIdx() &&
                prop->propName == "thornswoodTest";
            }) == 1);
  }

  SECTION("equipment")
  {
    p.Messages().clear();
    first.ReplaceEquipment(Equipment());
    REQUIRE(CountTo(p, 0, [&](IMessageBase* m) {
              auto eq = dynamic_cast<UpdateEquipmentMessage*>(m);
              return eq && eq->idx == first.GetIdx();
            }) == 1);
  }

  SECTION("taking an NPC is never followed by news that another has it")
  {
    p.Messages().clear();
    host.remoteId = first.GetFormId();
    listener.OnHostAttempt(raw, host);
    REQUIRE(p.worldState.hosters.at(first.GetFormId()) == 0xff000000);
    REQUIRE(CountTo(p, 0, [&](IMessageBase* m) {
              auto prop = dynamic_cast<UpdatePropertyMessage*>(m);
              return prop && prop->idx == first.GetIdx() &&
                prop->propName == "isHostedByOther" &&
                prop->dataDump == "true";
            }) == 0);
    REQUIRE(CountTo(p, 0, [&](IMessageBase* m) {
              auto prop = dynamic_cast<UpdatePropertyMessage*>(m);
              return prop && prop->idx == first.GetIdx() &&
                prop->propName == "isHostedByOther";
            }) == 1);
  }
}
