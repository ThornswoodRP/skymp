#include "TestUtils.hpp"
#include "FormCallbacks.h"
#include "MpActor.h"
#include "MsgType.h"
#include "PartOne.h"
#include <catch2/catch_all.hpp>

#include <simdjson.h>

extern espm::Loader& GetEspmLoader();

// Utilities for testing

void AttachSkyrimFiles(PartOne& server)
{
  server.AttachEspm(&GetEspmLoader());
}

MpChangeForm ThroughSave(const MpChangeForm& changeForm)
{
  static simdjson::dom::parser parser;
  const std::string dump = MpChangeForm::ToJson(changeForm).dump();
  simdjson::dom::element element = parser.parse(dump).value();
  return MpChangeForm::JsonToChangeForm(element);
}

std::string DumpChangeForm(const MpChangeForm& changeForm)
{
  return MpChangeForm::ToJson(changeForm).dump();
}

Inventory::Entry Worn(uint32_t baseId, uint32_t count)
{
  Inventory::ExtraData extra;
  extra.worn_ = true;
  return Inventory::Entry(baseId, count, extra);
}

std::underlying_type_t<Constants> ToUnderlying(Constants constant)
{
  return static_cast<std::underlying_type_t<Constants>>(constant);
}

bool IsCmakeOptionSpecified(const std::string& optionValue)
{
  return !optionValue.empty() && optionValue != "OFF";
}

const char* GetDataDir()
{
  return IsCmakeOptionSpecified(UNIT_DATA_DIR) ? UNIT_DATA_DIR
                                               : SKYRIM_DIR "/Data";
}

std::string MakeMessage(const nlohmann::json& j)
{
  std::string s;
  s += (char)Networking::MinPacketId;
  s += j.dump();
  return s;
}

void DoMessage(PartOne& partOne, Networking::UserId id,
               const nlohmann::json& j)
{
  std::string s = MakeMessage(j);
  PartOne* ptr = &partOne;
  PartOne::HandlePacket(ptr, id, Networking::PacketType::Message,
                        reinterpret_cast<Networking::PacketData>(s.data()),
                        s.size());
}

void DoConnect(PartOne& partOne, Networking::UserId id)
{
  PartOne* ptr = &partOne;
  PartOne::HandlePacket(ptr, id, Networking::PacketType::ServerSideUserConnect,
                        nullptr, 0);
}

void DoDisconnect(PartOne& partOne, Networking::UserId id)
{
  PartOne* ptr = &partOne;
  PartOne::HandlePacket(
    ptr, id, Networking::PacketType::ServerSideUserDisconnect, nullptr, 0);
}

void DoUpdateMovement(PartOne& partOne, uint32_t actorFormId,
                      Networking::UserId userId)
{
  auto jMyMovement = jMovement;
  jMyMovement["idx"] = dynamic_cast<MpActor*>(
                         partOne.worldState.LookupFormById(actorFormId).get())
                         ->GetIdx();
  DoMessage(partOne, userId, jMyMovement);
}

std::shared_ptr<FakeListener> FakeListener::New()
{
  return std::shared_ptr<FakeListener>(new FakeListener);
}

void FakeListener::OnConnect(Networking::UserId userId)
{
  ss << "OnConnect(" << userId << ")" << std::endl;
}

void FakeListener::OnDisconnect(Networking::UserId userId)
{
  ss << "OnDisconnect(" << userId << ")" << std::endl;
}

void FakeListener::OnCustomPacket(Networking::UserId userId,
                                  const simdjson::dom::element& content)
{
  ss << "OnCustomPacket(" << userId << ", " << simdjson::minify(content) << ")"
     << std::endl;
}

bool FakeListener::OnMpApiEvent(const GameModeEvent&)
{
  return true;
}

std::string FakeListener::str()
{
  return ss.str();
}

void FakeListener::clear()
{
  ss = std::stringstream();
}
