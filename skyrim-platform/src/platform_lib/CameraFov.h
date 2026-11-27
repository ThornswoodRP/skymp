#pragma once
#include <cmath>

// The angle sp.setCameraFOV gives the player's camera, in degrees
// (Thornswood #7200). A perspective camera needs an angle above 0 and below
// 180: at 0 or 180 its frustum has no width or no depth, and NaN or infinity
// would go into the frustum the renderer draws with. No game types here, so
// misc/tests/camera_fov_test.cpp runs it without the game.
namespace CameraFov {
inline bool IsValid(double degrees)
{
  return std::isfinite(degrees) && degrees > 0.0 && degrees < 180.0;
}
}
