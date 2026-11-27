#pragma once
#include "RecordHeader.h"
#include <set>
#include <string>
#include <vector>

#pragma pack(push, 1)

namespace espm {

class RACE final : public RecordHeader
{
public:
  static constexpr auto kType = "RACE";

  enum Flags : uint32_t
  {
    kPlayable = 0x00000001,
    kFaceGenHead = 0x00000002,
    kChild = 0x00000004,
    kTiltFrontBack = 0x00000008,
    kTiltLeftRight = 0x00000010,
    kNoShadow = 0x00000020,
    kSwims = 0x00000040,
    kFlies = 0x00000080,
    kWalks = 0x00000100,
    kImmobile = 0x00000200,
    kNotPushable = 0x00000400,
    kNoCombatInWater = 0x00000800,
    kNoRotatingToHeadTrack = 0x00001000,
    kDontShowBloodSpray = 0x00002000,
    kDontShowBloodDecal = 0x00004000,
    kUsesHeadTrackAnims = 0x00008000,
    kSpellsAlignWMagicNode = 0x00010000,
    kUseWorldRaycastsForFootIK = 0x00020000,
    kAllowRagdollCollision = 0x00040000,
    kRegenHPInCombat = 0x00080000,
    kCantOpenDoors = 0x00100000,
    kAllowPCDialogue = 0x00200000,
    kNoKnockdowns = 0x00400000,
    kAllowPickpocket = 0x00800000,
    kAlwaysUseProxyController = 0x01000000,
    kDontShowWeaponBlood = 0x02000000,
    kOverlayHeadPartList = 0x04000000,
    kOverrideHeadPartList = 0x08000000,
    kCanPickupItems = 0x10000000,
    kAllowMultipleMembraneShaders = 0x20000000,
    kCanDualWield = 0x40000000,
    kAvoidsRoads = 0x80000000
  };

  struct Data
  {
    uint32_t flags = 0;
    float startingHealth = 0.f;
    float startingMagicka = 0.f;
    float startingStamina = 0.f;
    float healRegen = 0.f;
    float magickaRegen = 0.f;
    float staminaRegen = 0.f;
    float unarmedDamage = 0.f;
    float unarmedReach = 0.f;

    std::set<uint32_t> spells = {};

    // The race's attacks (ATKD then ATKE, Thornswood #6720): the event its
    // behavior plays for each, and that attack's ATKD flags.
    enum AttackFlags : uint32_t
    {
      kAttackIgnoreWeapon = 0x01,
      kAttackBash = 0x02,
      kAttackPower = 0x04,
      kAttackLeft = 0x08,
      kAttackRotating = 0x10
    };
    struct Attack
    {
      uint32_t flags = 0;
      std::string event;
    };
    std::vector<Attack> attacks = {};
    // ATKR, as stored: the race whose attacks this one uses. A form id local
    // to the record's file.
    uint32_t attackRace = 0;
  };

  Data GetData(CompressedFieldsCache& compressedFieldsCache) const noexcept;
};

static_assert(sizeof(RACE) == sizeof(RecordHeader));

}

#pragma pack(pop)
