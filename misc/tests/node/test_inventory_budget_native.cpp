#include "../../../skyrim-platform/src/platform_se/skyrim_platform/InventoryApplyScheduler.h"
#include "../../../skyrim-platform/src/platform_se/skyrim_platform/InventoryApplyPump.h"
#include "../../../skyrim-platform/src/platform_se/skyrim_platform/InventoryQueueFence.h"
#include "../../../viet/include/TaskQueue.h"
#include <cassert>
#include <iostream>
#include <limits>

using namespace InventoryApply;
static Operation Units(uint32_t count, std::function<UnitResult(uint32_t)> run, uint32_t cost = 1) {
  return {count, 1, cost, std::move(run)};
}
int main() {
  // Counterfactual is the actual old queue, not a mirror of its algorithm.
  Viet::TaskQueue<> old;
  uint32_t oldCalls = 0;
  for (uint32_t i = 0; i < 100000; ++i) old.AddTask([&](const Viet::Void&) { ++oldCalls; });
  old.Update({});
  assert(oldCalls == 100000);

  uint64_t clock = 0;
  Scheduler scheduler([&] { return clock; });
  uint32_t first = 0, later = 0;
  auto a = scheduler.Submit(0x14, 1, {Units(100000, [&](uint32_t n) { first += n; return UnitResult::Ran; })}, [] { return true; });
  auto b = scheduler.Submit(0x15, 2, {Units(1, [&](uint32_t n) { later += n; return UnitResult::Ran; })}, [] { return true; });
  const auto frame = scheduler.Tick();
  assert(frame.mutations <= Scheduler::MutationBudget && first < 100000 && later == 1);
  assert(a->GetState() == State::Pending && b->GetState() == State::Drained);
  while (scheduler.HasPending()) assert(scheduler.Tick().mutations <= Scheduler::MutationBudget);
  assert(first == 100000 && a->GetState() == State::Drained);

  // Maximum canonical count is one compact remaining counter. Cancellation
  // must finish without iterating the billions of unissued units.
  uint32_t maxCalls = 0;
  auto max = scheduler.Submit(0x14, 3, {Units(std::numeric_limits<uint32_t>::max(), [&](uint32_t) { ++maxCalls; return UnitResult::Ran; })}, [] { return true; });
  scheduler.Tick();
  assert(maxCalls == 16);
  max->Cancel();
  assert(max->GetState() == State::Cancelled);
  scheduler.Tick();
  assert(maxCalls == 16 && !scheduler.HasPending());

  // Budget measures actual callback work, including an indivisible unit.
  uint32_t costly = 0;
  auto c = scheduler.Submit(0x14, 4, {Units(100, [&](uint32_t) { ++costly; clock += 3000000; return UnitResult::Ran; }, 3)}, [] { return true; });
  assert(scheduler.Tick().mutations == 3 && costly == 1 && c->GetState() == State::Pending);
  c->Cancel(); scheduler.Tick();

  // Per-reference descriptor order and exact counts survive yielding.
  std::vector<int> order;
  auto ordered = scheduler.Submit(0x14, 5, {
    Units(20, [&](uint32_t) { order.push_back(1); return UnitResult::Ran; }),
    Units(1, [&](uint32_t) { order.push_back(2); return UnitResult::Ran; }, 3)
  }, [] { return true; });
  while (scheduler.HasPending()) scheduler.Tick();
  assert(order.size() == 21 && order.back() == 2 && ordered->GetState() == State::Drained);
  for (size_t i = 0; i < 20; ++i) assert(order[i] == 1);

  // New work cannot bypass a still-running reference or malformed validation.
  auto pending = scheduler.Submit(0x14, 6, {Units(30, [](uint32_t) { return UnitResult::Ran; })}, [] { return true; });
  bool rejected = false;
  try { scheduler.Submit(0x14, 7, {Units(1, [](uint32_t) { return UnitResult::Ran; })}, [] { return true; }); }
  catch (const std::logic_error&) { rejected = true; }
  assert(rejected && pending->GetState() == State::Pending);
  rejected = false;
  try { scheduler.Submit(0x14, 7, {Units(1, [](uint32_t) { return UnitResult::Ran; }, 0)}, [] { return true; }); }
  catch (const std::invalid_argument&) { rejected = true; }
  assert(rejected && pending->GetState() == State::Pending);
  pending->Cancel(); scheduler.Tick();

  // An applied-then-thrown unit is terminal uncertain, never silently replayed.
  uint32_t applied = 0;
  auto unknown = scheduler.Submit(0x14, 8, {Units(100, [&](uint32_t) -> UnitResult { ++applied; throw std::runtime_error("after native add"); })}, [] { return true; });
  scheduler.Tick(); scheduler.Tick();
  assert(applied == 1 && unknown->GetState() == State::Uncertain && !scheduler.HasPending());

  // Native scope/life/load cancellation does not resolve a reused identity.
  bool current = true;
  auto lifetime = scheduler.Submit(0x14, 9, {Units(100, [&](uint32_t) { ++applied; return UnitResult::Ran; })}, [&] { return current; });
  current = false;
  scheduler.Tick();
  assert(lifetime->GetState() == State::Cancelled && applied == 1);
  auto replacement = scheduler.Submit(0x14, 10, {Units(1, [&](uint32_t) { ++applied; return UnitResult::Ran; })}, [] { return true; });
  scheduler.CancelReference(0x14, 9); // A late event for a different pointer.
  scheduler.Tick();
  assert(replacement->GetState() == State::Drained && applied == 2);

  // Cancellation is not quiescent until the indivisible running unit exits.
  std::shared_ptr<Job> during;
  bool sawBarrier = false;
  during = scheduler.Submit(0x14, 11, {Units(100, [&](uint32_t) {
    ++applied; during->Cancel(); sawBarrier = during->GetState() == State::Cancelling;
    return UnitResult::Ran;
  })}, [] { return true; });
  scheduler.Tick(); scheduler.Tick();
  assert(sawBarrier && applied == 3 && during->GetState() == State::Cancelled);

  // A paused operation yields rather than spinning or claiming completion.
  auto paused = scheduler.Submit(0x14, 12, {Units(100, [](uint32_t) { return UnitResult::Deferred; })}, [] { return true; });
  const auto pausedFrame = scheduler.Tick();
  assert(pausedFrame.visits == 1 && paused->GetState() == State::Pending);
  scheduler.CancelAll(); scheduler.Tick();
  assert(paused->GetState() == State::Cancelled);

  // Deferred work rotates before yielding. Two blocked references cannot
  // starve the active one behind them, even across successive updates.
  auto pausedA = scheduler.Submit(0x14, 14, {Units(10, [](uint32_t) { return UnitResult::Deferred; })}, [] { return true; });
  auto pausedB = scheduler.Submit(0x15, 15, {Units(10, [](uint32_t) { return UnitResult::Deferred; })}, [] { return true; });
  uint32_t available = 0;
  auto active = scheduler.Submit(0x16, 16, {Units(1, [&](uint32_t) { ++available; return UnitResult::Ran; })}, [] { return true; });
  scheduler.Tick(); scheduler.Tick(); scheduler.Tick();
  assert(available == 1 && active->GetState() == State::Drained);
  assert(pausedA->GetState() == State::Pending && pausedB->GetState() == State::Pending);
  scheduler.CancelAll(); scheduler.Tick();

  // A reentrant game callback cannot execute the same still-pending unit.
  uint32_t reentrant = 0;
  auto nested = scheduler.Submit(0x14, 17, {Units(1, [&](uint32_t) {
    ++reentrant;
    const auto ignored = scheduler.Tick();
    assert(ignored.mutations == 0 && ignored.visits == 0);
    return UnitResult::Ran;
  })}, [] { return true; });
  scheduler.Tick();
  assert(reentrant == 1 && nested->GetState() == State::Drained && !scheduler.HasPending());

  // An exceptional clock/infrastructure callback cannot leave Tick latched.
  bool throwClock = true;
  Scheduler clockFailure([&]() -> uint64_t { if (throwClock) throw std::runtime_error("clock fixture"); return 0; });
  uint32_t afterClockFailure = 0;
  auto clockJob = clockFailure.Submit(0x14, 18, {Units(1, [&](uint32_t) { ++afterClockFailure; return UnitResult::Ran; })}, [] { return true; });
  try { clockFailure.Tick(); assert(false); } catch (const std::runtime_error&) {}
  throwClock = false; clockFailure.Tick();
  assert(afterClockFailure == 1 && clockJob->GetState() == State::Drained);

  // The Tick guard must not block legitimate cancellation or new references
  // submitted by a game callback; both remain inside the shared budget.
  std::shared_ptr<Job> callbackJob, submitted;
  uint32_t callbackUnits = 0;
  callbackJob = scheduler.Submit(0x14, 19, {Units(10, [&](uint32_t) {
    ++callbackUnits; callbackJob->Cancel();
    submitted = scheduler.Submit(0x15, 20, {Units(1, [&](uint32_t) { ++callbackUnits; return UnitResult::Ran; })}, [] { return true; });
    return UnitResult::Ran;
  })}, [] { return true; });
  const auto callbackFrame = scheduler.Tick();
  assert(callbackFrame.mutations == 2 && callbackUnits == 2 && callbackJob->GetState() == State::Cancelled && submitted->GetState() == State::Drained);

  // Old queue position fences/unrelated tasks run between native chunks.
  Viet::TaskQueue<> queue;
  uint32_t queuedAdds = 0;
  auto large = scheduler.Submit(0x14, 13, {Units(100000, [&](uint32_t n) { queuedAdds += n; return UnitResult::Ran; })}, [] { return true; });
  uint32_t callbacksQueued = 0;
  Pump pump(scheduler, [&](std::function<void()> task) {
    ++callbacksQueued;
    queue.AddTask([task](const Viet::Void&) {task();});
  });
  pump.Queue(); pump.Queue(); pump.Queue();
  assert(callbacksQueued == 1);
  bool unrelated = false;
  queue.AddTask([&](const Viet::Void&) { unrelated = true; });
  auto position = CreateInventoryQueueFence(queue);
  queue.Update({});
  assert(unrelated && position() && queuedAdds == 16 && large->GetState() == State::Pending && callbacksQueued == 2);
  while (large->GetState() == State::Pending) queue.Update({});
  assert(queuedAdds == 100000 && large->GetState() == State::Drained);
  // Retired pump tickets cannot act on a new load's jobs. The old and new
  // callbacks may both occupy the queue, but only one can tick this frame.
  uint32_t replacedAdds = 0;
  auto retired = scheduler.Submit(0x14, 21, {Units(100, [&](uint32_t) {++replacedAdds; return UnitResult::Ran;})}, [] {return true;});
  pump.Queue(); pump.Cancel();
  auto fresh = scheduler.Submit(0x14, 22, {Units(100, [&](uint32_t) {++replacedAdds; return UnitResult::Ran;})}, [] {return true;});
  pump.Queue(); queue.Update({});
  assert(retired->GetState() == State::Cancelled && replacedAdds == 16 && fresh->GetState() == State::Pending);
  pump.Cancel(); queue.Update({});
  assert(replacedAdds == 16 && fresh->GetState() == State::Cancelled);
  bool rejectQueue = true;
  Pump failure(scheduler, [&](std::function<void()> task) {
    if (rejectQueue) throw std::runtime_error("queue allocation fixture");
    queue.AddTask([task](const Viet::Void&) {task();});
  });
  auto rejectedPump = scheduler.Submit(0x14, 23, {Units(1, [&](uint32_t) {++replacedAdds; return UnitResult::Ran;})}, [] {return true;});
  try {failure.Queue(); assert(false);} catch (const std::runtime_error&) {}
  assert(rejectedPump->GetState() == State::Cancelled && replacedAdds == 16);
  rejectQueue = false;
  auto recoveredPump = scheduler.Submit(0x14, 24, {Units(1, [&](uint32_t) {++replacedAdds; return UnitResult::Ran;})}, [] {return true;});
  failure.Queue(); queue.Update({});
  assert(recoveredPump->GetState() == State::Drained && replacedAdds == 17);
  std::cout << "inventory scheduler: count/time/fairness/FIFO/cancel/uncertainty controls passed\n";
}
