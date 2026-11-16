#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace lx::host {

enum class TaskPriority : int {
  Background = 0,
  Interactive = 1,
};

struct CancelToken {
  std::atomic<bool> cancelled{false};

  void cancel() { cancelled.store(true, std::memory_order_release); }
  bool is_cancelled() const { return cancelled.load(std::memory_order_acquire); }
};

/**
 * Priority thread pool with per-key debouncing, cancellation, and
 * single-active-writer guarantee per key (§10.1, §10.2).
 */
class Scheduler {
 public:
  using Clock = std::chrono::steady_clock;
  using TaskFn = std::function<void(const std::shared_ptr<CancelToken>&)>;

  explicit Scheduler(size_t worker_count = 0,
                     std::chrono::milliseconds default_debounce = std::chrono::milliseconds(30));
  ~Scheduler();

  Scheduler(const Scheduler&) = delete;
  Scheduler& operator=(const Scheduler&) = delete;

  /**
   * Schedules (or reschedules) work for `key`. Any previous pending or active
   * task for `key` has its CancelToken signalled. Runs after `debounce`.
   * Returns the CancelToken for the newly scheduled task.
   */
  std::shared_ptr<CancelToken> schedule(const std::string& key,
                                        TaskFn fn,
                                        TaskPriority priority = TaskPriority::Interactive,
                                        std::chrono::milliseconds debounce = std::chrono::milliseconds(-1));

  /**
   * Cancels any pending or running task for `key`.
   */
  void cancel(const std::string& key);

  /**
   * Removes key tracking state (cancelling any pending task first).
   */
  void remove_key(const std::string& key);

  /**
   * Blocks until any pending or running task for `key` finishes.
   */
  void wait_for(const std::string& key);

  /**
   * Blocks until all currently pending and running tasks finish.
   */
  void wait_idle();

  void shutdown();

  size_t worker_count() const { return workers_.size(); }

 private:
  struct KeyEntry {
    uint64_t generation{0};
    bool has_pending{false};
    bool is_running{false};
    TaskPriority priority{TaskPriority::Interactive};
    Clock::time_point due_at{};
    uint64_t seq{0};
    TaskFn fn;
    std::shared_ptr<CancelToken> pending_cancel;
    std::shared_ptr<CancelToken> running_cancel;
  };

  void worker_loop();
  bool has_ready_or_stopping_locked(Clock::time_point now) const;
  std::string pick_next_ready_locked(Clock::time_point now);
  Clock::time_point earliest_due_locked() const;

  std::chrono::milliseconds default_debounce_;
  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::condition_variable idle_cv_;
  bool stopping_{false};
  uint64_t seq_counter_{0};
  std::unordered_map<std::string, KeyEntry> keys_;
  std::vector<std::thread> workers_;
};

}  // namespace lx::host
