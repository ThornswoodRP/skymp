// Thornswood #4511. ScriptCallWaiting, the Promises runScriptFunction
// (ScriptCallApi.cpp) keeps until the Papyrus VM says each call returned, and
// which values setScriptVariable takes into a script's variable.
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
  { // A value goes in only as the variable's own kind.
    using K = ScriptValueKind;
    using J = JsValueKind;
    assert(ScriptValueMismatch(K::Int, J::Number).empty());
    assert(ScriptValueMismatch(K::Float, J::Number).empty());
    assert(ScriptValueMismatch(K::Bool, J::Boolean).empty());
    assert(ScriptValueMismatch(K::String, J::String).empty());
    assert(ScriptValueMismatch(K::Object, J::Object).empty());
    assert(ScriptValueMismatch(K::Object, J::Nothing).empty());
    assert(!ScriptValueMismatch(K::Int, J::String).empty());
    assert(!ScriptValueMismatch(K::Int, J::Nothing).empty());
    assert(!ScriptValueMismatch(K::Bool, J::Number).empty());
    assert(!ScriptValueMismatch(K::String, J::Nothing).empty());
    assert(!ScriptValueMismatch(K::Object, J::Number).empty());
    assert(!ScriptValueMismatch(K::Object, J::Other).empty());
    assert(!ScriptValueMismatch(K::Other, J::Number).empty());
  }
  { // An array is filled in place at its own size, never a None one.
    assert(ScriptArrayMismatch(true, 128, true, 128).empty());
    assert(ScriptArrayMismatch(true, 0, true, 0).empty());
    assert(ScriptArrayMismatch(false, 0, true, 128) ==
           "the array is None, so there is nothing to fill");
    assert(ScriptArrayMismatch(true, 128, false, 0) ==
           "it is an array, so an array is wanted");
    assert(ScriptArrayMismatch(true, 128, true, 127) ==
           "it holds 128 values and 127 were given");
  }
  std::cout << "PASS run script function: waiting calls" << std::endl;
  std::cout << "PASS set script variable: kinds and array sizes" << std::endl;
  return 0;
}
