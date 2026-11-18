#include "GameplayMenuPolicy.h"
#include <array>
#include <cstdio>
#include <string_view>

namespace P = GameplayMenuPolicy;
int failed = 0;
#define CHECK(x) do { if (!(x)) { ++failed; std::printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)

struct Menu { std::uint32_t flags; };

int main()
{
  // Every unrelated bit, including cursor/modal and freeze-frame screenshot
  // pause, must survive. Native pause and background-freeze are different bits.
  constexpr auto unrelated = ~(P::Pauses | P::FrozenBackground);
  constexpr std::array names = { "TweenMenu", "InventoryMenu", "MagicMenu",
    "MapMenu", "StatsMenu", "MessageBoxMenu", "Console", "CustomMenu" };
  for (std::string_view name : names) {
    Menu m{ unrelated };
    int calls = 0, pauses = 0;
    // The creator and PostCreate have already returned. The original Show
    // callback can still set pause/freeze, as Stats does in the actual game.
    auto result = P::Show(m, true, [&] {
      ++calls;
      m.flags |= P::Pauses | P::FrozenBackground;
      return 37;
    });
    pauses += (m.flags & P::Pauses) != 0;
    CHECK(result == 37 && calls == 1);
    CHECK(m.flags == unrelated);
    CHECK(pauses == 0);
    pauses -= (m.flags & P::Pauses) != 0;
    CHECK(pauses == 0);
    std::printf("checked %.*s\n", int(name.size()), name.data());
  }

  // Offline and unloaded-world paths keep all flags and balanced counts.
  Menu offline{ unrelated };
  int count = 0;
  P::Show(offline, false, [&] { offline.flags |= P::Pauses | P::FrozenBackground; return 0; });
  CHECK(offline.flags == 0xffffffffu);
  count += (offline.flags & P::Pauses) != 0;
  CHECK(count == 1);
  count -= (offline.flags & P::Pauses) != 0;
  CHECK(count == 0);

  P::Session session;
  CHECK(!session.Update(false, false));
  CHECK(!session.Update(true, false)); // connection at the title/loading screen
  CHECK(session.Update(true, true));
  CHECK(session.Update(false, true)); // destroy/recreate while a world is live
  CHECK(session.Update(true, true));
  CHECK(session.Update(false, true));
  CHECK(!session.Update(false, false)); // explicit end plus world unload
  CHECK(!session.Update(false, true)); // later offline world is not latched
  std::printf("%d failures\n", failed);
  return failed ? 1 : 0;
}
