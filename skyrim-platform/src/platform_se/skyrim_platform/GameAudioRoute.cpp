#include "GameAudioRoute.h"

#include <Windows.h>
#include <mmdeviceapi.h>
#include <roapi.h>
#include <winstring.h>
#include <wrl/client.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace {
using Microsoft::WRL::ComPtr;

// Windows Volume Mixer stores a preference for an application identity.
// This experimental bridge uses its WinRT factory on versions where the
// known interface is present. It only targets this Skyrim process.
constexpr GUID kAudioPolicy{ 0xab3d4648, 0xe242, 0x459f,
                            { 0xb0, 0x2f, 0x54, 0x1c, 0x70, 0x30, 0x63, 0x24 } };
constexpr size_t kSetEndpointSlot = 25;
constexpr size_t kGetEndpointSlot = 26;
using SetEndpoint = HRESULT(STDMETHODCALLTYPE*)(IUnknown*, UINT32, EDataFlow,
                                                 ERole, HSTRING);
using GetEndpoint = HRESULT(STDMETHODCALLTYPE*)(IUnknown*, UINT32, EDataFlow,
                                                 ERole, HSTRING*);

struct Runtime
{
  HRESULT status = RoInitialize(RO_INIT_MULTITHREADED);
  ~Runtime()
  {
    if (SUCCEEDED(status))
      RoUninitialize();
  }
};

struct WinString
{
  HSTRING value = nullptr;
  ~WinString() { WindowsDeleteString(value); }
};

std::optional<std::wstring> ReadSelection()
{
  std::wstring executable(32768, L'\0');
  DWORD length = GetModuleFileNameW(nullptr, executable.data(),
                                   static_cast<DWORD>(executable.size()));
  if (length == 0 || length >= executable.size())
    return std::nullopt;
  executable.resize(length);
  auto file = std::filesystem::path(executable).parent_path() / L"Data" /
    L"SKSE" / L"Plugins" / L"ThornswoodAudioOutput.txt";
  std::ifstream stream(file, std::ios::binary);
  if (!stream)
    return std::nullopt;

  std::array<char, 1025> bytes{};
  stream.read(bytes.data(), bytes.size());
  auto count = static_cast<int>(stream.gcount());
  if (stream.bad() || count >= static_cast<int>(bytes.size()))
    return std::nullopt;
  if (count == 0)
    return std::wstring{};

  std::wstring selection(static_cast<size_t>(count), L'\0');
  int chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                 bytes.data(), count, selection.data(), count);
  if (chars <= 0)
    return std::nullopt;
  selection.resize(chars);
  return selection.find_first_of(std::wstring{ L'\0', L'\r', L'\n' }) ==
      std::wstring::npos
    ? std::optional(std::move(selection))
    : std::nullopt;
}

bool ActiveOutput(const std::wstring& id)
{
  ComPtr<IMMDeviceEnumerator> enumerator;
  auto hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                             IID_PPV_ARGS(&enumerator));
  if (FAILED(hr))
    return false;
  // Initialize the game's Core Audio identity before asking Windows to
  // change its per-app preference.
  ComPtr<IMMDevice> current;
  if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &current)))
    return false;
  if (id.empty())
    return true;
  ComPtr<IMMDevice> endpoint;
  hr = enumerator->GetDevice(id.c_str(), &endpoint);
  if (FAILED(hr) || !endpoint)
    return false;
  DWORD state = 0;
  if (FAILED(endpoint->GetState(&state)) || state != DEVICE_STATE_ACTIVE)
    return false;
  ComPtr<IMMEndpoint> info;
  EDataFlow flow = eAll;
  return SUCCEEDED(endpoint.As(&info)) &&
    SUCCEEDED(info->GetDataFlow(&flow)) && flow == eRender;
}

bool ApplyPreference(const std::wstring& id)
{
  Runtime runtime;
  if (FAILED(runtime.status) && runtime.status != RPC_E_CHANGED_MODE)
    return false;
  if (!ActiveOutput(id))
    return false;

  WinString runtimeClass;
  constexpr wchar_t className[] = L"Windows.Media.Internal.AudioPolicyConfig";
  if (FAILED(WindowsCreateString(className, ARRAYSIZE(className) - 1,
                                 &runtimeClass.value)))
    return false;
  ComPtr<IUnknown> policy;
  if (FAILED(RoGetActivationFactory(
        runtimeClass.value, kAudioPolicy,
        reinterpret_cast<void**>(policy.GetAddressOf()))) || !policy)
    return false;

  WinString target;
  if (!id.empty()) {
    auto name = std::wstring(LR"(\\?\SWD#MMDEVAPI#)") + id +
      L"#{e6327cad-dcec-4949-ae8a-991e976a79d2}";
    if (FAILED(WindowsCreateString(name.c_str(), static_cast<UINT32>(name.size()),
                                   &target.value)))
      return false;
  }

  void** vtable = *reinterpret_cast<void***>(policy.Get());
  auto get = reinterpret_cast<GetEndpoint>(vtable[kGetEndpointSlot]);
  auto set = reinterpret_cast<SetEndpoint>(vtable[kSetEndpointSlot]);
  DWORD pid = GetCurrentProcessId();
  constexpr std::array roles{ eConsole, eMultimedia, eCommunications };
  std::array<WinString, roles.size()> previous;
  for (size_t index = 0; index < roles.size(); ++index) {
    if (FAILED(get(policy.Get(), pid, eRender, roles[index],
                   &previous[index].value)))
      return false;
  }
  for (size_t index = 0; index < roles.size(); ++index) {
    if (SUCCEEDED(set(policy.Get(), pid, eRender, roles[index], target.value)))
      continue;
    for (size_t restored = 0; restored < index; ++restored)
      set(policy.Get(), pid, eRender, roles[restored], previous[restored].value);
    return false;
  }
  return true;
}
}

GameAudioRoute::Result GameAudioRoute::ApplyAtStartup() noexcept
{
  try {
    auto selection = ReadSelection();
    if (!selection)
      return Result::NotConfigured;
    return ApplyPreference(*selection) ? Result::Applied : Result::Unavailable;
  } catch (...) {
    // An unavailable preference must never stop Skyrim from starting.
    return Result::Unavailable;
  }
}
