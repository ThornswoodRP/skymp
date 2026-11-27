// The angles sp.setCameraFOV takes (Thornswood #7200): only an angle above 0
// and below 180 degrees reaches the player camera. CameraFov.h has no game
// types, so this runs the same check the platform runs.
#include "CameraFov.h"
#include <cstdio>
#include <limits>

int failed = 0;
#define CHECK(x)                                                              \
  do {                                                                        \
    if (!(x)) {                                                               \
      ++failed;                                                               \
      std::printf("FAIL line %d: %s\n", __LINE__, #x);                        \
    }                                                                         \
  } while (0)

int main()
{
  // The Settings page's range and the game's own default.
  CHECK(CameraFov::IsValid(60));
  CHECK(CameraFov::IsValid(65));
  CHECK(CameraFov::IsValid(90));
  CHECK(CameraFov::IsValid(140));
  CHECK(CameraFov::IsValid(0.5));
  CHECK(CameraFov::IsValid(179.5));

  // No frustum at these.
  CHECK(!CameraFov::IsValid(0));
  CHECK(!CameraFov::IsValid(-1));
  CHECK(!CameraFov::IsValid(180));
  CHECK(!CameraFov::IsValid(360));
  CHECK(!CameraFov::IsValid(std::numeric_limits<double>::quiet_NaN()));
  CHECK(!CameraFov::IsValid(std::numeric_limits<double>::infinity()));
  CHECK(!CameraFov::IsValid(-std::numeric_limits<double>::infinity()));

  if (failed) {
    std::printf("%d failed\n", failed);
    return 1;
  }
  std::printf("camera field of view: all passed\n");
  return 0;
}
