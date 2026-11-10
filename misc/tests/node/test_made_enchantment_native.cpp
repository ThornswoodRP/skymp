// Thornswood #4513. The game-free half of createMadeEnchantment
// (MadeEnchantment.h): which requests it builds.
#include "MadeEnchantment.h"
#include <cstdio>
#include <limits>

static int pass = 0, fail = 0;
static void Yes(const char* what, bool cond)
{
  std::printf("%s  %s\n", cond ? "PASS" : "FAIL", what);
  (cond ? pass : fail)++;
}

static MadePotion::Request Good()
{
  MadePotion::Request r;
  r.formId = 0xff8e0001;
  r.effectIds = { 0x3eb15 };
  r.magnitudes = { 10 };
  r.areas = { 0 };
  r.durations = { 1 };
  return r;
}

int main()
{
  Yes("an enchantment the server sends is well formed",
      MadeEnchantment::IsWellFormed(Good()));

  auto r = Good();
  r.effectIds.push_back(0x3eaf3);
  r.magnitudes.push_back(25);
  r.areas.push_back(0);
  r.durations.push_back(0);
  Yes("two effects, with Twin Souls, are built",
      MadeEnchantment::IsWellFormed(r));
  r.effectIds.push_back(0x39e51);
  r.magnitudes.push_back(5);
  r.areas.push_back(0);
  r.durations.push_back(0);
  Yes("three are not", !MadeEnchantment::IsWellFormed(r));

  r = Good();
  r.formId = 0xff000a01;
  Yes("an id the game hands out itself is not built over",
      !MadeEnchantment::IsWellFormed(r));

  r = Good();
  r.effectIds.clear();
  r.magnitudes.clear();
  r.areas.clear();
  r.durations.clear();
  Yes("an enchantment with nothing in it is not built",
      !MadeEnchantment::IsWellFormed(r));

  r = Good();
  r.magnitudes[0] = std::numeric_limits<double>::infinity();
  Yes("an endless magnitude is not built", !MadeEnchantment::IsWellFormed(r));

  std::printf("%d passed, %d failed\n", pass, fail);
  return fail ? 1 : 0;
}
