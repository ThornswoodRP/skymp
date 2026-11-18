#pragma once
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

namespace GameplayMenuPatches {
using Bytes = std::vector<std::uint8_t>;
inline void Address(Bytes& out, std::uint64_t address)
{
  for (unsigned i = 0; i < 8; ++i) out.push_back(std::uint8_t(address >> (i * 8)));
}
inline void Jump(Bytes& out, std::uint64_t target)
{
  out.insert(out.end(), { 0xff, 0x25, 0, 0, 0, 0 });
  Address(out, target);
}
inline Bytes Branch(std::uint8_t opcode, std::uintptr_t site,
                    std::uintptr_t target, std::size_t size)
{
  const auto distance = std::int64_t(target) - std::int64_t(site + 5);
  if (size < 5 || distance < std::numeric_limits<std::int32_t>::min() ||
      distance > std::numeric_limits<std::int32_t>::max())
    throw std::runtime_error("menu hook is outside its branch range");
  Bytes out(size, 0x90);
  out[0] = opcode;
  const auto relative = std::int32_t(distance);
  std::memcpy(out.data() + 1, &relative, sizeof(relative));
  return out;
}
inline Bytes Show(std::uint64_t helper, std::uint64_t resume)
{
  // Entered by JMP at a native call site with RCX/RDX and the caller's
  // 32-byte shadow space intact. CALL adds exactly the original return slot.
  Bytes out{ 0x48, 0xb8 };
  Address(out, helper);
  out.insert(out.end(), { 0xff, 0xd0, 0x83, 0xf8, 1 }); // call rax; cmp eax,1
  Jump(out, resume); // indirect JMP preserves that comparison's flags
  return out;
}
inline Bytes Background(std::uint64_t enabled, std::uint64_t resume, bool stats)
{
  // R11 is volatile but live at these non-call sites: preserve it. No CALL,
  // allocation, or C++ object access occurs here. The x64 load observes the
  // lock-free uint32 atomic published by the main-thread owner.
  Bytes out{ 0x41, 0x53, 0x49, 0xbb }; // push r11; mov r11,enabled
  Address(out, enabled);
  out.insert(out.end(), { 0x41, 0x83, 0x3b, 0, 0x41, 0x5b, 0x75,
                          std::uint8_t(stats ? 4 : 3) });
  if (stats) {
    out.insert(out.end(), { 0x83, 0x4e, 0x1c, 0x20 });
    // Replay the whole displaced comparison, including its flags.
    out.insert(out.end(), { 0x48, 0x83, 0xbe, 0xa8, 2, 0, 0, 0 });
  } else {
    out.insert(out.end(), { 0x83, 0xc8, 0x20 });
  }
  Jump(out, resume);
  return out;
}

inline Bytes StatsInput(std::uint64_t enabled, std::uint64_t resume,
                        std::uint64_t blocked)
{
  Bytes out{ 0x41, 0x53, 0x49, 0xbb };
  Address(out, enabled);
  out.insert(out.end(), { 0x41, 0x83, 0x3b, 0, 0x41, 0x5b,
                          0x75, 18, 0x84, 0xc0, 0x74, 14 });
  // Live-world UI now runs on the main thread. Bypass only this obsolete
  // worker-mode gate; offline replay TEST AL,AL and the original JNE target.
  Jump(out, blocked);
  Jump(out, resume);
  return out;
}
}
