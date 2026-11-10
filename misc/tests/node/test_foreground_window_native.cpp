// Thornswood #4523. The game-free half of win32.foregroundWindow
// (ForegroundWindow.h): what the answer says about the window in front.
#include "ForegroundWindow.h"
#include <cstdio>

static int pass = 0, fail = 0;
static void Yes(const char* what, bool cond)
{
  std::printf("%s  %s\n", cond ? "PASS" : "FAIL", what);
  (cond ? pass : fail)++;
}

int main()
{
  const auto game = ForegroundWindow::Describe(
    4242, 4242, "C:\\Games\\Skyrim Special Edition\\SkyrimSE.exe",
    "Skyrim Special Edition", "Skyrim Special Edition");
  Yes("the game's own window is the game", game.isGame && game.ourProcess);
  Yes("and is named by its exe's file name", game.exe == "SkyrimSE.exe");

  const auto other = ForegroundWindow::Describe(
    77, 4242, "C:\\Users\\x\\AppData\\Local\\Discord\\app\\Discord.exe",
    "Discord", "Chrome_WidgetWin_1");
  Yes("another program's window is not the game",
      !other.isGame && !other.ourProcess);
  Yes("and says which program it is",
      other.exe == "Discord.exe" && other.title == "Discord" &&
        other.className == "Chrome_WidgetWin_1");

  const auto unknown = ForegroundWindow::Describe(0, 0, "", "", "");
  Yes("a window Windows names no process for is never the game",
      !unknown.isGame && !unknown.ourProcess);

  Yes("a forward slash path is cut the same way",
      ForegroundWindow::ExeName("C:/x/y/Notepad.exe") == "Notepad.exe");
  Yes("a bare name stays as it is",
      ForegroundWindow::ExeName("Notepad.exe") == "Notepad.exe");

  std::printf("%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
