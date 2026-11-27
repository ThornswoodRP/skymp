#include "CameraApi.h"
#include "CameraFov.h"
#include "InvalidArgumentException.h"
#include "NullPointerException.h"

Napi::Value CameraApi::WorldPointToScreenPoint(const Napi::CallbackInfo& info)
{
  auto camera = RE::PlayerCamera::GetSingleton();
  if (!camera)
    throw NullPointerException("camera");
  auto camRoot = camera->cameraRoot;
  if (!camRoot)
    throw NullPointerException("camNode");
  auto n = camRoot->children.size();

  RE::NiCamera* niCamera = nullptr;
  for (uint16_t i = 0; i < n; ++i) {
    auto niAvObject = camRoot->children[i];
    if (!niAvObject)
      continue;
    niCamera = netimmerse_cast<RE::NiCamera*>(niAvObject.get());
    if (niCamera)
      break;
  }
  if (!niCamera)
    throw NullPointerException("matrix");

  auto length = info.Length();

  auto res = Napi::Array::New(info.Env(), length);

  char argNameForExtract[5] = "argX";

  for (size_t i = 0; i < length; ++i) {

    size_t charSizeT = static_cast<size_t>('0') + i;
    if (charSizeT <= 255 && i <= 9) {
      argNameForExtract[3] = static_cast<char>(charSizeT);
    }

    auto posExtracted =
      NapiHelper::ExtractNiPoint3(info[i], argNameForExtract);
    RE::NiPoint3 pos = { posExtracted[0], posExtracted[1], posExtracted[2] };
    float outX, outY, outZ;
    RE::NiCamera::WorldPtToScreenPt3(niCamera->worldToCam, niCamera->port, pos,
                                     outX, outY, outZ, 1.f);

    auto jsPos = Napi::Array::New(info.Env(), 3);
    jsPos.Set(static_cast<uint32_t>(0), Napi::Number::New(info.Env(), outX));
    jsPos.Set(static_cast<uint32_t>(1), Napi::Number::New(info.Env(), outY));
    jsPos.Set(static_cast<uint32_t>(2), Napi::Number::New(info.Env(), outZ));
    res.Set(static_cast<uint32_t>(i), jsPos);
  }

  return res;
}

// Thornswood #7200. Skyrim Platform had no call that sets the camera's field
// of view (SetCameraFOV is only a console command, and a plugin cannot run
// one), so the Settings page's field of view never reached the camera. The
// player camera keeps the angle it draws the world with in worldFOV, and the
// one it draws the first person arms with in firstPersonFOV: the same two
// angles the console's fov command takes.
Napi::Value CameraApi::SetCameraFov(const Napi::CallbackInfo& info)
{
  auto worldFov = NapiHelper::ExtractDouble(info[0], "worldFov");
  auto firstPersonFov = NapiHelper::ExtractDouble(info[1], "firstPersonFov");
  if (!CameraFov::IsValid(worldFov))
    throw InvalidArgumentException("worldFov", worldFov);
  if (!CameraFov::IsValid(firstPersonFov))
    throw InvalidArgumentException("firstPersonFov", firstPersonFov);

  auto camera = RE::PlayerCamera::GetSingleton();
  if (!camera)
    throw NullPointerException("camera");
  camera->worldFOV = static_cast<float>(worldFov);
  camera->firstPersonFOV = static_cast<float>(firstPersonFov);
  return info.Env().Undefined();
}
