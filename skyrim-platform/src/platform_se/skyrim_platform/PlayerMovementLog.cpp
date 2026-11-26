#include "PlayerMovementLog.h"
#include "CharacterStateLog.h"

#include <chrono>

namespace {
CharacterStateLog g_log;

int64_t NowMs()
{
  return std::chrono::duration_cast<std::chrono::milliseconds>(
           std::chrono::steady_clock::now().time_since_epoch())
    .count();
}
}

// Thornswood #6886. Read only: nothing here writes to the character, its
// controller or the camera.
void LogPlayerMovement()
{
  auto player = RE::PlayerCharacter::GetSingleton();
  if (!player) {
    return;
  }
  auto controller = player->GetCharController();
  if (!controller) {
    return;
  }
  auto camera = RE::PlayerCamera::GetSingleton();

  CharacterStateLog::State s;
  s.current = static_cast<uint32_t>(controller->context.currentState);
  s.wanted = static_cast<uint32_t>(controller->wantState);
  s.flags = static_cast<uint32_t>(controller->flags.underlying());
  s.firstPerson = camera && camera->IsInFirstPerson();

  int leftOut = 0;
  if (!g_log.Write(s, NowMs(), &leftOut)) {
    return;
  }
  const auto pos = player->GetPosition();
  const auto kept = s.flags & CharacterStateLog::kFlagsKept;
  spdlog::info("movement: controller state {} wants {} flags {:#x}{}{}{}{}, "
               "{} person, at {:.0f},{:.0f},{:.0f}, water at {:.0f}, pitch "
               "{:.2f}{}",
               s.current, s.wanted, kept,
               (kept & (1u << 25)) ? " can pitch" : "",
               (kept & (1u << 23)) ? " shoulders under water" : "",
               (kept & (1u << 27)) ? " no character collisions" : "",
               (kept & (1u << 1)) ? " no gravity on ground" : "",
               s.firstPerson ? "first" : "third", pos.x, pos.y, pos.z,
               controller->waterHeight, controller->pitchAngle,
               leftOut ? fmt::format(" ({} changes before this one not "
                                     "written, the minute's lines were spent)",
                                     leftOut)
                       : std::string());
}
