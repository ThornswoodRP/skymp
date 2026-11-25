#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <iterator>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

// Inventory work alone yields between frames. Viet::TaskQueue and its fences
// retain their queue-position/FIFO contract for every other caller.
namespace InventoryApply {
enum class State { Pending, Drained, Cancelling, Cancelled, Uncertain };
enum class UnitResult { Ran, Deferred, Cancelled };

struct Operation {
  uint32_t remaining = 0;
  uint32_t quantityPerUnit = 1;
  uint32_t cost = 1;
  std::function<UnitResult(uint32_t)> run;
};

class Scheduler;
class Job {
public:
  State GetState() const {
    const auto value = state.load(std::memory_order_acquire);
    if (value == State::Pending && cancelRequested.load(std::memory_order_acquire)) {
      return running.load(std::memory_order_acquire) ? State::Cancelling : State::Cancelled;
    }
    return value;
  }
  void Cancel() {
    cancelRequested.store(true, std::memory_order_release);
    if (!running.load(std::memory_order_acquire)) {
      auto pending = State::Pending;
      state.compare_exchange_strong(pending, State::Cancelled, std::memory_order_acq_rel);
    }
  }
  uint32_t ReferenceId() const { return referenceId; }

private:
  friend class Scheduler;
  uint32_t referenceId = 0;
  uintptr_t identity = 0;
  std::vector<Operation> operations;
  size_t cursor = 0;
  std::function<bool()> isCurrent;
  std::atomic<State> state{State::Pending};
  std::atomic<bool> cancelRequested{false}, running{false};
};

class Scheduler {
public:
  static constexpr uint32_t MutationBudget = 16;
  static constexpr uint32_t VisitBudget = 64;
  static constexpr uint64_t TimeBudgetNs = 2000000;
  using Clock = std::function<uint64_t()>;
  struct TickResult { uint32_t mutations = 0, visits = 0; };

  explicit Scheduler(Clock now = [] {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
  }) : now(std::move(now)) {}

  std::shared_ptr<Job> Submit(uint32_t referenceId, uintptr_t identity,
                             std::vector<Operation> operations,
                             std::function<bool()> isCurrent) {
    // All descriptors are checked before cancelling/replacing existing work.
    if (!referenceId || !identity || !isCurrent) throw std::invalid_argument("Invalid inventory scope");
    for (const auto& op : operations) {
      if (!op.quantityPerUnit || !op.cost || op.cost > MutationBudget || !op.run)
        throw std::invalid_argument("Invalid inventory operation");
    }
    std::lock_guard<std::recursive_mutex> lock(mutex);
    auto existing = byReference.find(referenceId);
    if (existing != byReference.end()) {
      const auto state = (*existing->second)->GetState();
      if (state == State::Pending || state == State::Cancelling)
        throw std::logic_error("Previous inventory work has not settled");
      jobs.erase(existing->second);
      byReference.erase(existing);
    }
    auto job = std::make_shared<Job>();
    job->referenceId = referenceId;
    job->identity = identity;
    job->operations = std::move(operations);
    job->isCurrent = std::move(isCurrent);
    if (job->operations.empty()) {
      job->state.store(State::Drained, std::memory_order_release);
      return job;
    }
    jobs.push_back(job);
    byReference[referenceId] = std::prev(jobs.end());
    return job;
  }

  void CancelReference(uint32_t referenceId, uintptr_t identity = 0) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    auto found = byReference.find(referenceId);
    if (found != byReference.end() && (!identity || (*found->second)->identity == identity))
      (*found->second)->Cancel();
  }
  std::shared_ptr<Job> ReferenceBarrier(uint32_t referenceId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    auto found = byReference.find(referenceId);
    return found == byReference.end() ? nullptr : *found->second;
  }
  void CancelAll() {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    for (auto at = jobs.begin(); at != jobs.end();) {
      auto job = *at;
      ++at;
      job->Cancel();
      // Retain a running unit's canonical barrier until it returns. Other
      // cancelled descriptors can be released even if a load clears the
      // queued pump and no future update ever arrives.
      if (job->GetState() == State::Cancelled) Erase(job);
    }
  }
  bool HasPending() const {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return !jobs.empty();
  }

  TickResult Tick() {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (ticking) return {};
    ticking = true;
    struct ResetTick { bool& flag; ~ResetTick() { flag = false; } } reset{ticking};
    TickResult result;
    size_t budgetSkips = 0;
    const auto started = now();
    while (!jobs.empty() && result.visits < VisitBudget && result.mutations < MutationBudget && now() - started < TimeBudgetNs) {
      ++result.visits;
      auto at = jobs.begin();
      auto job = *at;
      // One unit per turn, so a long job cannot starve later references.
      jobs.splice(jobs.end(), jobs, at);
      if (job->GetState() != State::Pending) { Erase(job); continue; }
      auto& op = job->operations[job->cursor];
      if (!op.remaining) {
        Advance(job);
        continue;
      }
      if (op.cost > MutationBudget - result.mutations) {
        if (++budgetSkips >= jobs.size()) break;
        continue;
      }
      budgetSkips = 0;
      job->running.store(true, std::memory_order_release);
      bool deferred = false;
      try {
        if (job->cancelRequested.load(std::memory_order_acquire) || !job->isCurrent()) {
          job->state.store(State::Cancelled, std::memory_order_release);
        } else {
          // Reserve the indivisible add/equip/refresh cost before starting.
          result.mutations += op.cost;
          const auto quantity = std::min(op.remaining, op.quantityPerUnit);
          const auto outcome = op.run(quantity);
          if (outcome == UnitResult::Cancelled) job->state.store(State::Cancelled, std::memory_order_release);
          else if (outcome == UnitResult::Deferred) deferred = true;
          else {
            op.remaining -= quantity;
            if (!op.remaining) Advance(job);
          }
        }
      } catch (...) {
        // A mutation may have happened before an exception. Never replay this
        // unit; the caller must observe actual inventory before a new plan.
        job->state.store(State::Uncertain, std::memory_order_release);
      }
      job->running.store(false, std::memory_order_release);
      if (job->cancelRequested.load(std::memory_order_acquire) && job->GetState() != State::Uncertain)
        job->state.store(State::Cancelled, std::memory_order_release);
      if (job->GetState() != State::Pending) Erase(job);
      if (deferred) break;
    }
    return result;
  }

private:
  void Advance(const std::shared_ptr<Job>& job) {
    if (++job->cursor == job->operations.size()) {
      job->state.store(State::Drained, std::memory_order_release);
      // The caller of Advance may still be inside the indivisible unit.
      if (!job->running.load(std::memory_order_acquire)) Erase(job);
    }
  }
  void Erase(const std::shared_ptr<Job>& job) {
    auto found = byReference.find(job->referenceId);
    if (found != byReference.end() && *found->second == job) {
      jobs.erase(found->second);
      byReference.erase(found);
    }
  }
  Clock now;
  bool ticking = false;
  mutable std::recursive_mutex mutex;
  std::list<std::shared_ptr<Job>> jobs;
  std::unordered_map<uint32_t, std::list<std::shared_ptr<Job>>::iterator> byReference;
};
}
