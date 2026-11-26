#include "InteractionLoadCancel.h"
#include "InteractionLoadPatch.h"
#include <atomic>

// Thornswood #6884. Why, and what the bytes are: platform_lib/InteractionLoadPatch.h.
namespace {
namespace P = InteractionLoadPatch;
SKSE::Trampoline g_trampoline("Interaction load cancel");
std::atomic_bool g_installed{ false };
}

bool InteractionLoadCancel::Install()
{
  if (g_installed.load()) return true;
  if (REL::Module::get().version() != REL::Version(1, 6, 1170, 0)) {
    logger::error("interaction load: unsupported runtime; the game's uncancelled furniture load is left as it is");
    return false;
  }
  const auto window = REL::ID(39798).address() + P::kWindowOffset;
  if (!P::Matches(reinterpret_cast<const std::uint8_t*>(window))) {
    logger::error("interaction load: native bytes differ at {:X}; no patch installed",
                  window - REL::Module::get().base());
    return false;
  }
  const auto natives = P::Resolve(window);
  if (reinterpret_cast<std::uintptr_t>(natives.clearSet) != REL::ID(38275).address() ||
      reinterpret_cast<std::uintptr_t>(natives.cancel) != REL::ID(63971).address()) {
    logger::error("interaction load: the window does not call the set clear and the cancel it was read with; no patch installed");
    return false;
  }
  P::g_natives = natives;

  g_trampoline.create(64);
  const auto stubBytes = P::Stub(reinterpret_cast<std::uintptr_t>(&P::EndInteractionLoad));
  auto* stub = g_trampoline.allocate(stubBytes.size());
  std::memcpy(stub, stubBytes.data(), stubBytes.size());
  const auto replacement = P::Replacement(window, reinterpret_cast<std::uintptr_t>(stub));
  REL::safe_write(window + P::kSite, replacement.data(), replacement.size());
  g_installed.store(true);
  logger::info("interaction load: a delivered interaction set no longer skips the cancel of its queued load");
  return true;
}
