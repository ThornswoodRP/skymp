#pragma once
#include "MessageBase.h"
#include "MsgType.h"
#include <type_traits>

// Thornswood #4507. A melee hit killed, and the server picked a killmove for
// it: every machine that sees the target plays the pair, the attacker (idx)
// on the target (targetIdx). Sent to them before the death itself.
struct KillMoveMessage : public MessageBase<KillMoveMessage>
{
  static constexpr auto kMsgType =
    std::integral_constant<char, static_cast<char>(MsgType::KillMove)>{};

  template <class Archive>
  void Serialize(Archive& archive)
  {
    archive.Serialize("t", kMsgType)
      .Serialize("idx", idx)
      .Serialize("targetIdx", targetIdx)
      .Serialize("idle", idle);
  }

  uint32_t idx = 0;
  uint32_t targetIdx = 0;
  // The IDLE record's form id in the load order
  uint32_t idle = 0;
};
