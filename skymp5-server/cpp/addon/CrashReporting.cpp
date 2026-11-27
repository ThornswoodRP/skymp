#include "CrashReporting.h"

#ifdef _WIN32
#  include <cstdio>

#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
// THORNSWOOD (Thornswood #7247): without WIN32_LEAN_AND_MEAN, Windows.h
// declares Winsock 1, which cannot share a file with the Winsock 2 that
// SLikeNet includes; the addon compiles in batches, so it would.
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <Windows.h>
#  include <werapi.h>
#endif

std::string CrashReporting::Allow()
{
#ifdef _WIN32
  SetErrorMode(GetErrorMode() & ~SEM_NOGPFAULTERRORBOX);
  HRESULT hr = WerSetFlags(WER_FAULT_REPORTING_NO_UI);
  if (FAILED(hr)) {
    char text[96];
    std::snprintf(text, sizeof(text),
                  "WerSetFlags(WER_FAULT_REPORTING_NO_UI) failed: 0x%08lX",
                  static_cast<unsigned long>(hr));
    return text;
  }
#endif
  return {};
}
