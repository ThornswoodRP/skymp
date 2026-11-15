#pragma once
// Thornswood #6201. The way for the Papyrus call behind SkyrimPlatform's
// 'update' event. TESModPlatform::Update sends one call to TESModPlatform.Add
// at a time and sends the next only once the one before it has ended. A call
// ends in one of two ways: the VM runs its callback when Add returns, or the
// VM lets go of the callback when it throws the call away without running
// it, as a game load does to the calls waiting in it. Both ends open the way.
//
// On 15 Nov 2026 a new character's game ran 'tick' for a minute after the
// creation hall loaded and never ran 'update' again. Update runs after every
// 'tick', so each frame it found the way shut: the call it had sent never came
// back through Add. Only Add's return opened the way then, and a load event
// opened it again for the calls a load throws away, so a call thrown away
// with no load event after it left the way shut for the rest of the session.
//
// This holds no game types, so a test builds it over a stand-in VM.
#include <atomic>
#include <cstdint>

class PapyrusUpdateWay
{
public:
  // How long a call waits before the log hears about it once.
  static constexpr int64_t kWaitWrittenMs = 5000;

  // Whether a new call may be sent at now (milliseconds). While a call is on
  // its way, the first time it has waited kWaitWrittenMs, writeWait is set to
  // how long it has waited, for the caller to write to the log.
  bool Open(int64_t now, int64_t* writeWait = nullptr)
  {
    if (onItsWay == nullptr) {
      return true;
    }
    const int64_t waited = now - sentAt;
    if (waited >= kWaitWrittenMs && !waitWritten.exchange(true) && writeWait) {
      *writeWait = waited;
    }
    return false;
  }

  // call is on its way from now. Only called while the way is open.
  void Send(const void* call, int64_t now)
  {
    sentAt = now;
    waitWritten = false;
    onItsWay = call;
  }

  struct Ended
  {
    bool wasOnItsWay = false; // false: another call is on its way, or none
    int64_t waited = 0;       // milliseconds from Send to this end
    bool waitWasWritten = false;
  };

  // call has ended, by running or by being thrown away. Opens the way when
  // call is the one on its way; does nothing for any other. What it reads of
  // the call is read before the way opens, since Send writes it again after.
  Ended End(const void* call, int64_t now)
  {
    Ended res;
    res.waited = now - sentAt;
    res.waitWasWritten = waitWritten;
    const void* expected = call;
    res.wasOnItsWay = onItsWay.compare_exchange_strong(expected, nullptr);
    return res;
  }

private:
  std::atomic<const void*> onItsWay = nullptr;
  std::atomic<int64_t> sentAt = 0;
  std::atomic<bool> waitWritten = false;
};
