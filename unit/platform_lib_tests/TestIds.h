#pragma once
#include <cstdint>

// Thornswood: ids several platform_lib tests use, said once here. The unit
// tests are compiled in batches of eight files (unity build,
// cmake/thornswood_unity_build.cmake), so two files of a batch that each said
// one of these in their own anonymous namespace would be one file saying it
// twice.

constexpr uint32_t kK = 37; // DIK_K, the game's scan code for K

constexpr uint32_t kPlayer = 0x14;      // the player's reference
constexpr uint32_t kOther = 0xff000a2c; // somebody else's
