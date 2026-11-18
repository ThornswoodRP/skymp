#pragma once
#include <utility>

// Only immutable function pointers are retained. Delegate/menu/argument
// lifetimes remain owned by Skyrim, and all calls run on the main UI owner.
template <class Args> class SleepWaitChoice
{
public:
  using Callback = void(const Args&);
  bool Bind(Callback** slot, Callback* expectedOk, Callback* expectedCancel,
            Callback* wrapper, bool supported, bool ownsHandler)
  {
    if (!supported || !ownsHandler || !slot || !expectedOk || !expectedCancel)
      return false;
    if (*slot == wrapper)
      return original == expectedOk && cancel == expectedCancel;
    if (*slot != expectedOk || (original && original != expectedOk)) return false;
    original = *slot;
    cancel = expectedCancel;
    *slot = wrapper;
    return true;
  }

  template <class Hide, class Notify>
  bool Run(const Args& args, bool /*live*/, bool /*cancelVerified*/,
           Hide&& /*hide*/, Notify&& /*notify*/) const
  {
    if (!original) return false;
    original(args);
    return true;
  }
private:
  Callback* original = nullptr;
  Callback* cancel = nullptr;
};
