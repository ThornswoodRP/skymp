#include "InventoryShortcut.h"

int main()
{
  using namespace InventoryShortcut;
  if (!Matches(true, true, 23, 23) || !Matches(true, true, 38, 38))
    return 1;
  if (Matches(false, true, 23, 23) || Matches(true, false, 23, 23) ||
      Matches(true, true, 23, 38) || Matches(true, true, 0, 0) ||
      Matches(true, true, 255, 255) || Matches(true, true, 256, 256))
    return 2;
  constexpr auto opened = NewLifetime(0, true);
  constexpr auto shut = NewLifetime(opened, false);
  constexpr auto reopened = NewLifetime(shut, true);
  if (!Current(opened, opened) || Current(0, 0) ||
      Current(opened, shut) || Current(opened, reopened) ||
      Current(shut, shut))
    return 3;
  if (Request(opened, 0, 23, 23) != 0 ||
      Request(opened, opened, 23, 23) != opened ||
      Request(reopened, opened, 23, 23) != 0 ||
      Request(reopened, reopened, 23, 38) != 0)
    return 4;
}
