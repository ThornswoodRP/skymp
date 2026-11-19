#pragma once

namespace GameAudioRoute {
enum class Result { NotConfigured, Applied, Unavailable };

// Apply the local launcher selection before Skyrim opens its audio engine.
// An absent preference does not change Windows' per-app settings.
Result ApplyAtStartup() noexcept;
}
