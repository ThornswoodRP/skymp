// Thornswood #4511. ScriptCallWaiting, the Promises runScriptFunction
// (ScriptCallApi.cpp) keeps until the Papyrus VM says each call returned.
#include <cassert>
#include <iostream>
#include <string>
#include "ScriptCall.h"

int main()
{
  { // Each call is settled once, by its own id.
    ScriptCallWaiting<std::string> w;
    const uint64_t a = w.Add("first");
    const uint64_t b = w.Add("second");
    assert(a != b);
    assert(w.Waiting() == 2);
    auto got = w.Take(b);
    assert(got && *got == "second");
    assert(!w.Take(b));
    assert(w.Waiting() == 1);
    got = w.Take(a);
    assert(got && *got == "first");
    assert(w.Waiting() == 0);
  }
  { // An id nobody was given settles nothing.
    ScriptCallWaiting<std::string> w;
    w.Add("only");
    assert(!w.Take(99));
    assert(w.Waiting() == 1);
  }
  { // After a reload lets go of everything, a call that returns late finds
    // nothing, and the ids handed out next are new ones.
    ScriptCallWaiting<std::string> w;
    const uint64_t old = w.Add("before the reload");
    w.Clear();
    assert(w.Waiting() == 0);
    const uint64_t fresh = w.Add("after the reload");
    assert(fresh != old);
    assert(!w.Take(old));
    auto got = w.Take(fresh);
    assert(got && *got == "after the reload");
  }
  { // An argument is a string or a flag.
    ScriptCallArg s;
    s.isString = true;
    s.text = "../QuickLootIE/DefaultConfig.json";
    ScriptCallArg f;
    f.flag = false;
    assert(s.isString && !f.isString && !f.flag);
  }
  std::cout << "PASS run script function: waiting calls" << std::endl;
  return 0;
}
