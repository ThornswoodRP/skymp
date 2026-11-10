#pragma once

#include <cstdint>
#include <string>

// THORNSWOOD (Thornswood #4523). The game-free half of win32.foregroundWindow
// (Win32Api.cpp): what the answer says about the window that has the
// keyboard.
//
// The start screen ignores keys typed while another program has the
// keyboard, and it asks this which window that is. The window belongs to the
// game when it belongs to the game's own process: the game's main window, the
// browser drawn over it and any dialog of its own all take keys meant for
// the game, and nothing else does.
namespace ForegroundWindow {

struct Answer
{
  bool isGame = false;
  bool ourProcess = false;
  std::string exe;
  std::string title;
  std::string className;
};

// The file name of a full path, either slash.
inline std::string ExeName(const std::string& path)
{
  const auto cut = path.find_last_of("\\/");
  return cut == std::string::npos ? path : path.substr(cut + 1);
}

// windowPid is the process the window belongs to (0 when Windows did not
// say), ourPid the game's own.
inline Answer Describe(uint32_t windowPid, uint32_t ourPid,
                       const std::string& exePath, const std::string& title,
                       const std::string& className)
{
  Answer a;
  a.ourProcess = windowPid != 0 && windowPid == ourPid;
  a.isGame = a.ourProcess;
  a.exe = ExeName(exePath);
  a.title = title;
  a.className = className;
  return a;
}
}
