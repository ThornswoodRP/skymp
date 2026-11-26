// Thornswood #6201. PapyrusUpdateWay, the way for the Papyrus call behind the
// 'update' event (PapyrusTESModPlatform.cpp), over a stand-in VM that runs a
// call, throws one away, or holds one.
#include "PapyrusUpdateWay.h"
#include <cstdint>
#include <iostream>

static int failed = 0;
static void Check(const char* name, bool ok)
{
  std::cout << (ok ? "PASS " : "FAIL ") << name << "\n";
  if (!ok)
    ++failed;
}

int main()
{
  int a = 0, b = 0, c = 0; // three calls, by address
  {
    PapyrusUpdateWay way;
    Check("the way starts open", way.Open(0));
    way.Send(&a, 10);
    Check("a call on its way shuts it", !way.Open(11));
    auto ran = way.End(&a, 30);
    Check("the call running opens it", ran.wasOnItsWay && way.Open(31));
    Check("and says how long it waited", ran.waited == 20);
    Check("the VM letting go of it after it ran does nothing",
          !way.End(&a, 40).wasOnItsWay && way.Open(41));
  }
  {
    // 15 Nov: a load threw the call away with no load event after it. The
    // way stayed shut, so no 'update' event came again.
    PapyrusUpdateWay way;
    way.Send(&a, 0);
    auto thrown = way.End(&a, 500); // the VM lets go of it, unrun
    Check("a call thrown away opens the way", thrown.wasOnItsWay);
    Check("so the next frame sends the next call", way.Open(501));
    way.Send(&b, 501);
    Check("which shuts it again", !way.Open(502));
  }
  {
    PapyrusUpdateWay way;
    way.Send(&a, 0);
    way.End(&a, 5);
    way.Send(&b, 6);
    Check("an old call ending late leaves the new one on its way",
          !way.End(&a, 7).wasOnItsWay && !way.Open(8));
    Check("the new one still opens it", way.End(&b, 9).wasOnItsWay);
    Check("a call never sent opens nothing", !way.End(&c, 10).wasOnItsWay);
  }
  {
    // A VM that holds the call is written to the log once, at five seconds.
    PapyrusUpdateWay way;
    way.Send(&a, 1000);
    int64_t w = -1;
    way.Open(1000 + PapyrusUpdateWay::kWaitWrittenMs - 1, &w);
    Check("a call waiting under five seconds is not written", w == -1);
    way.Open(1000 + PapyrusUpdateWay::kWaitWrittenMs, &w);
    Check("at five seconds it is written with how long it waited",
          w == PapyrusUpdateWay::kWaitWrittenMs);
    int64_t again = -1;
    way.Open(9000, &again);
    Check("not again before thirty seconds more", again == -1);
    auto late = way.End(&a, 9500);
    Check("its end says the wait was written, and how long it was",
          late.wasOnItsWay && late.waitWasWritten && late.waited == 8500);
    way.Send(&b, 9600);
    int64_t next = -1;
    way.Open(9600 + PapyrusUpdateWay::kWaitWrittenMs, &next);
    Check("the next call that waits is written again", next >= 0);
  }
  {
    // 25 Nov (#6885): after a quit to the main menu one call waited for good.
    // Its wait is written at 5 s and every 30 s after, six times at most.
    PapyrusUpdateWay way;
    way.Send(&a, 0);
    int writes = 0;
    int64_t last = -1;
    bool onTime = true;
    for (int64_t t = 0; t <= 600000; t += 16) {
      int64_t w = -1;
      way.Open(t, &w);
      if (w >= 0) {
        const int64_t due = PapyrusUpdateWay::kWaitWrittenMs +
          writes * PapyrusUpdateWay::kWaitWrittenAgainMs;
        if (w < due || w >= due + 16) {
          onTime = false;
        }
        ++writes;
        last = w;
      }
    }
    Check("a call that never ends is written at 5 s and each 30 s after",
          onTime && writes > 1);
    Check("six times at most", writes == PapyrusUpdateWay::kWaitWrittenTimes);
    Check("the last at 155 s", last >= 155000 && last < 155016);
    Check("and the way stays shut all along", !way.Open(600001));
    Check("its end still opens it", way.End(&a, 600002).wasOnItsWay);
    way.Send(&b, 600003);
    int64_t fresh = -1;
    way.Open(600003 + PapyrusUpdateWay::kWaitWrittenMs, &fresh);
    Check("and the next call that waits is written from the start again",
          fresh >= 0);
  }
  return failed ? 1 : 0;
}
