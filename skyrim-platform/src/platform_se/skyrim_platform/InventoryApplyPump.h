#pragma once
#include "InventoryApplyScheduler.h"

namespace InventoryApply {
// The adapter supplies the existing game's TaskQueue. Exactly one callback
// is outstanding, regardless of how many references are submitted. A queue
// update snapshots its callbacks, so continuations yield to the next frame.
class Pump {
public:
  using Enqueue = std::function<void(std::function<void()>)>;
  using CancelJobs = std::function<void()>;
  Pump(Scheduler& scheduler, Enqueue enqueue, CancelJobs cancelJobs = {})
    : scheduler(scheduler), enqueue(std::move(enqueue)),
      cancelJobs(cancelJobs ? std::move(cancelJobs) : [&scheduler] {scheduler.CancelAll();}) {}
  void Queue() {
    std::shared_ptr<int> ticket;
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (pending) return;
      ticket = std::make_shared<int>(0);
      pending = ticket;
    }
    try {
      enqueue([this, ticket] {
        {
          std::lock_guard<std::mutex> lock(mutex);
          if (pending != ticket) return;
          pending.reset();
        }
        try {
          scheduler.Tick();
          if (scheduler.HasPending()) Queue();
        } catch (...) {
          // Infrastructure failure must not replay the current native unit.
          Cancel();
          throw;
        }
      });
    } catch (...) {
      {
        std::lock_guard<std::mutex> lock(mutex);
        if (pending == ticket) pending.reset();
      }
      scheduler.CancelAll();
      throw;
    }
  }
  void Cancel() {
    // ==== retire inventory pump ticket ====
    {
      // Retire only the OLD ticket before scheduler cancellation. A new
      // submission racing after CancelAll must keep its newer ticket; doing
      // this in the reverse order can leave that job pending with no pump.
      std::lock_guard<std::mutex> lock(mutex);
      pending.reset();
    }
    // ==== end retire inventory pump ticket ====
    cancelJobs();
  }
private:
  Scheduler& scheduler;
  Enqueue enqueue;
  CancelJobs cancelJobs;
  std::mutex mutex;
  std::shared_ptr<int> pending;
};
}
