// Execute the production wrapper against a stand-in for the game's UI.
#include <cassert>
#include <cstdint>
#include <map>
#include <string>
namespace RE {
struct IMenu {
  enum class Flag : uint32_t { kPausesGame = 1, kFreezeFrameBackground = 32 };
  struct Flags {
    uint32_t value;
    void reset(Flag f) { value &= ~static_cast<uint32_t>(f); }
  } menuFlags;
};
struct JournalMenu { static constexpr const char* MENU_NAME = "Journal Menu"; };
using BSFixedString = std::string;
struct UI {
  using Create_t = IMenu*();
  struct Entry { Create_t* create = nullptr; };
  std::map<std::string, Entry> menuMap;
  static UI* instance;
  static UI* GetSingleton() { return instance; }
};
UI* UI::instance = nullptr;
}
namespace logger {
void error(const char*) {}
void info(const char*) {}
}
#include "../../../skyrim-platform/src/platform_se/skyrim_platform/JournalMenuUnpaused.cpp"
int calls = 0;
bool empty = false;
RE::IMenu* GameCreate()
{
  ++calls;
  return empty ? nullptr : new RE::IMenu{{0x421}};
}
int main()
{
  JournalMenuUnpaused::Install(); // No UI yet must not mark installation done.
  RE::UI ui;
  RE::UI::instance = &ui;
  JournalMenuUnpaused::Install(); // No creator yet must not mark it done either.
  ui.menuMap["Journal Menu"].create = GameCreate;
  ui.menuMap["Other Menu"].create = GameCreate;
  JournalMenuUnpaused::Install();
  auto wrapped = ui.menuMap["Journal Menu"].create;
  assert(wrapped != GameCreate);
  assert(ui.menuMap["Other Menu"].create == GameCreate);
  JournalMenuUnpaused::Install();
  assert(ui.menuMap["Journal Menu"].create == wrapped);
  int pauses = 0;
  for (int i = 0; i < 3; ++i) {
    auto menu = wrapped();
    assert(menu && menu->menuFlags.value == 0x400);
    if (menu->menuFlags.value & 1) ++pauses;
    assert(pauses == 0);
    if (menu->menuFlags.value & 1) --pauses;
    assert(pauses == 0);
    delete menu;
  }
  assert(calls == 3);
  empty = true;
  assert(wrapped() == nullptr);
}
