#pragma once
#include "MessageBase.h"
#include "MsgType.h"
#include <cstdint>
#include <optional>
#include <type_traits>
#include <vector>

struct HitMessage : public MessageBase<HitMessage>
{
  static constexpr auto kMsgType =
    std::integral_constant<char, static_cast<char>(MsgType::OnHit)>{};

  template <class Archive>
  void Serialize(Archive& archive)
  {
    archive.Serialize("t", kMsgType).Serialize("data", data);
  }

  struct Data
  {
    template <class Archive>
    void Serialize(Archive& archive)
    {
      archive.Serialize("aggressor", aggressor)
        .Serialize("isBashAttack", isBashAttack)
        .Serialize("isHitBlocked", isHitBlocked)
        .Serialize("isPowerAttack", isPowerAttack)
        .Serialize("isSneakAttack", isSneakAttack)
        .Serialize("projectile", projectile)
        .Serialize("source", source)
        .Serialize("target", target)
        .Serialize("killMoves", killMoves);
    }

    uint32_t aggressor = 0;
    bool isBashAttack = false;
    bool isHitBlocked = false;
    bool isPowerAttack = false;
    bool isSneakAttack = false;
    uint32_t projectile = 0;
    uint32_t source = 0;
    uint32_t target = 0;
    // Thornswood #4507. The killmoves the attacker's game judged fit for
    // this swing, in the idle tree's order, with the two conditions the
    // server owns left out (ShouldAttackKill and the KillMove global). The
    // server plays the first that fits only when the hit's death goes
    // through.
    std::optional<std::vector<uint32_t>> killMoves;
  };

  Data data;
};
