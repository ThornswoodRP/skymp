#include "Win32Api.h"
#include "ForegroundWindow.h"

Napi::Value Win32Api::LoadUrl(const Napi::CallbackInfo& info)
{
  auto str = NapiHelper::ExtractString(info[0], "url");
  if (str.substr(0, 8) != "https://") {
    throw std::runtime_error(
      "Permission denied, only 'https://' prefix is allowed");
  } else {
    // CoInitializeEx is needed to call this function, but it seems we already
    // have this call somewhere in code, related link:
    // https://docs.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shellexecutea
    ShellExecute(0, 0, str.c_str(), 0, 0, SW_SHOW);
  }
  return info.Env().Undefined();
}

Napi::Value Win32Api::ExitProcess(const Napi::CallbackInfo& info)
{
  RE::Main::GetSingleton()->quitGame = true;
  return info.Env().Undefined();
}

namespace {
std::string Utf8(const wchar_t* text, int length)
{
  if (length <= 0) {
    return {};
  }
  const int bytes =
    WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
  if (bytes <= 0) {
    return {};
  }
  std::string out(static_cast<size_t>(bytes), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text, length, out.data(), bytes, nullptr,
                      nullptr);
  return out;
}
}

// THORNSWOOD PATCH (Thornswood #4523). win32.foregroundWindow(): the window
// that has the keyboard, as { isGame, ourProcess, exe, title, className }, or
// null when no window has it (a locked screen). The start screen asks it so
// that keys typed in another program never pick a character; without it the
// guard never turned on. ForegroundWindow.h says what the answer means.
Napi::Value Win32Api::ForegroundWindow(const Napi::CallbackInfo& info)
{
  const auto env = info.Env();
  HWND window = GetForegroundWindow();
  if (!window) {
    return env.Null();
  }

  DWORD pid = 0;
  GetWindowThreadProcessId(window, &pid);

  wchar_t title[512] = {};
  const int titleLength = GetWindowTextW(window, title, 512);
  wchar_t className[256] = {};
  const int classLength = GetClassNameW(window, className, 256);

  std::string exePath;
  if (pid) {
    if (HANDLE process =
          OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
      wchar_t path[MAX_PATH] = {};
      DWORD size = MAX_PATH;
      if (QueryFullProcessImageNameW(process, 0, path, &size)) {
        exePath = Utf8(path, static_cast<int>(size));
      }
      CloseHandle(process);
    }
  }

  const auto answer = ::ForegroundWindow::Describe(
    pid, GetCurrentProcessId(), exePath, Utf8(title, titleLength),
    Utf8(className, classLength));

  auto res = Napi::Object::New(env);
  res.Set("isGame", Napi::Boolean::New(env, answer.isGame));
  res.Set("ourProcess", Napi::Boolean::New(env, answer.ourProcess));
  res.Set("exe", Napi::String::New(env, answer.exe));
  res.Set("title", Napi::String::New(env, answer.title));
  res.Set("className", Napi::String::New(env, answer.className));
  return res;
}
