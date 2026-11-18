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
    if (!supported || !ownsHandler || !slot || !expectedOk || !expectedCancel || !wrapper)
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
  bool Run(const Args& args, bool live, bool cancelVerified,
           Hide&& hide, Notify&& notify) const
  {
    if (!original) return false;
    if (!live) {
      original(args);
      return true;
    }
    if (cancelVerified) cancel(args);
    else std::forward<Hide>(hide)();
    std::forward<Notify>(notify)();
    return true;
  }
private:
  Callback* original = nullptr;
  Callback* cancel = nullptr;
};

// Close an offline picker before any live movie/input pass. Native Hide, not
// a changed flag or repaired counter, removes its original pause exactly once.
class SleepWaitHandoff
{
public:
  template <class Hide>
  void BeforeMessages(bool /*live*/, bool /*pickerOpen*/, Hide&& /*hide*/)
  {
  }
  bool AllowMovies(bool /*pickerOpen*/)
  {
    return true;
  }
private:
  bool wasLive = false, closing = false;
};
