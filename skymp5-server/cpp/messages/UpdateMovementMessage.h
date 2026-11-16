#pragma once

#include "MessageBase.h"
#include "MsgType.h"
#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>

struct UpdateMovementMessage : public MessageBase<UpdateMovementMessage>
{
  static constexpr auto kMsgType =
    std::integral_constant<char, static_cast<char>(MsgType::UpdateMovement)>{};

  struct Data
  {
    template <class Archive>
    void Serialize(Archive& archive)
    {
      archive.Serialize("worldOrCell", worldOrCell)
        .Serialize("pos", pos)
        .Serialize("rot", rot)
        .Serialize("direction", direction)
        .Serialize("healthPercentage", healthPercentage)
        .Serialize("speed", speed)
        .Serialize("runMode", runMode)
        .Serialize("isInJumpState", isInJumpState)
        .Serialize("isSneaking", isSneaking)
        .Serialize("isBlocking", isBlocking)
        .Serialize("isWeapDrawn", isWeapDrawn)
        .Serialize("isDead", isDead)
        .Serialize("lookAt", lookAt)
        .Serialize("teleportSeq", teleportSeq)
        .Serialize("moveSeq", moveSeq)
        .Serialize("settled", settled);
    }

    uint32_t worldOrCell = 0;
    std::array<float, 3> pos{ 0, 0, 0 };
    std::array<float, 3> rot{ 0, 0, 0 };
    float direction = 0;
    float healthPercentage = 0;
    float speed = 0;

    // flags & optionals
    std::string runMode = "Standing";
    bool isInJumpState = false;
    bool isSneaking = false;
    bool isBlocking = false;
    bool isWeapDrawn = false;
    bool isDead = false;
    std::optional<std::array<float, 3>> lookAt = std::nullopt;

    // THORNSWOOD. Sent by a client for its own character only: the
    // teleportSeq of the newest TeleportMessage/TeleportMessage2 it has
    // carried out, 0 before the first. The server uses it to tell a packet
    // sent before the client moved from one sent after.
    std::optional<uint32_t> teleportSeq = std::nullopt;

    // THORNSWOOD (Thornswood #6247). A move is sent when it changes, not on
    // a timer, so the newest one has to be the one that sticks. From a
    // client: its own count for this actor, one more for every move it
    // sends. From the server: the server's count for this actor, one more
    // for every move it passes on. Whoever reads a move drops one numbered
    // at or below the last it took (ActionListener::OnUpdateMovement,
    // RemoteServer.onUpdateMovementMessage).
    std::optional<uint32_t> moveSeq = std::nullopt;

    // THORNSWOOD (Thornswood #6247). Set on the move a client sends once
    // the actor has stopped changing: nothing follows it until something
    // changes, so it goes reliably, and the server passes it on reliably.
    // The moves before it go unreliably, each one replaced 130 ms later.
    std::optional<bool> settled = std::nullopt;
  };

  template <class Archive>
  void Serialize(Archive& archive)
  {
    archive.Serialize("t", kMsgType)
      .Serialize("idx", idx)
      .Serialize("data", data);
  }

  uint32_t idx = 0;
  Data data;
};
