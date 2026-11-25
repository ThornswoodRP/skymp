#pragma once
#include "InventoryApplyScheduler.h"

namespace InventoryApply {
// The adapter supplies the existing game's TaskQueue. Exactly one callback
// is outstanding, regardless of how many references are submitted. A queue
// update snapshots its callbacks, so continuations yield to the next frame.
class Pump {
public:
  using Enqueue = std::function<void(std::function<void()>)>;
  Pump(Scheduler& scheduler, Enqueue enqueue) : scheduler(scheduler), enqueue(std::move(enqueue)) {}
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
    scheduler.CancelAll();
    std::lock_guard<std::mutex> lock(mutex);
    pending.reset();
  }
private:
  Scheduler& scheduler;
  Enqueue enqueue;
  std::mutex mutex;
  std::shared_ptr<int> pending;
};
}
