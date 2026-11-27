#pragma once
#include <cstdint>

// Thornswood: forms of Skyrim.esm that several tests use, said once here.
// The unit tests are compiled in batches of eight files (unity build,
// cmake/thornswood_unity_build.cmake), so two files of a batch that each said
// one of these in their own anonymous namespace would be one file saying it
// twice.

constexpr uint32_t kForge = 0x1ad6e;
constexpr uint32_t kIronDagger = 0x1397e;
constexpr uint32_t kSword = 0x12eb7; // Iron Sword
constexpr uint32_t kGold = 0xf;
constexpr uint32_t kPettySoulGem = 0x2e4e2;

// Ingredients, with the magic effects each has
constexpr uint32_t kBlueMountainFlower = 0x77e1c; // 3eb15 3eb25 3eaf3 73f2b
constexpr uint32_t kWheat = 0x4b0ba;              // 3eb15 3eaf3 73f2c 10de5f

// Magic effects
constexpr uint32_t kRestoreHealth = 0x3eb15;
constexpr uint32_t kFortifyHealth = 0x3eaf3;
constexpr uint32_t kResistMagic = 0x39e51;
