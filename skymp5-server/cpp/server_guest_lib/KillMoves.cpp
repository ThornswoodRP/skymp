#include "KillMoves.h"
#include "libespm/CombineBrowser.h"
#include "libespm/IDLE.h"
#include "libespm/LookupResult.h"
#include <array>

namespace {
// Skyrim.esm's killmove roots (Thornswood Ops issues/4507, read from the
// masters on 27 Nov 2026)
constexpr std::array<uint32_t, 2> kAttackRoots = {
  0x100e8b, // KillMoveFrontSideRoot00
  0x100f16, // KillMoveBackSideRoot00
};
constexpr std::array<uint32_t, 2> kPowerAttackRoots = {
  0x24cd4, // KillMoveFrontSideRoot
  0xe8458, // KillMoveBackSideRoot
};

// Deeper than any chain in the masters (seven at most), and short enough
// that a loop in a broken plugin's ANAM ends.
constexpr int kMaxDepth = 32;
}

bool KillMoves::FitsAttack(const espm::CombineBrowser& browser,
                           espm::CompressedFieldsCache& cache, uint32_t idleId,
                           bool isPowerAttack)
{
  const auto& roots = isPowerAttack ? kPowerAttackRoots : kAttackRoots;

  {
    const auto self = browser.LookupById(idleId);
    if (!self.rec || self.rec->GetType() != espm::IDLE::kType ||
        reinterpret_cast<const espm::IDLE*>(self.rec)
          ->GetData(cache)
          .animationEvent.empty()) {
      return false;
    }
  }

  uint32_t id = idleId;
  for (int depth = 0; depth < kMaxDepth; ++depth) {
    const auto lookup = browser.LookupById(id);
    if (!lookup.rec || lookup.rec->GetType() != espm::IDLE::kType) {
      return false;
    }
    const auto data =
      reinterpret_cast<const espm::IDLE*>(lookup.rec)->GetData(cache);
    const uint32_t parentId = lookup.ToGlobalId(data.parentId);
    if (!parentId) {
      return false;
    }
    for (auto root : roots) {
      if (parentId == root) {
        return true;
      }
    }
    id = parentId;
  }
  return false;
}

std::optional<uint32_t> KillMoves::Pick(
  const espm::CombineBrowser& browser, espm::CompressedFieldsCache& cache,
  const std::vector<uint32_t>& candidates, bool isPowerAttack)
{
  for (auto candidate : candidates) {
    if (FitsAttack(browser, cache, candidate, isPowerAttack)) {
      return candidate;
    }
  }
  return std::nullopt;
}
