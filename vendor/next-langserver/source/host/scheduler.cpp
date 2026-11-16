#include "host/scheduler.hpp"

#include <algorithm>

namespace lx::host {

Scheduler::Scheduler(size_t worker_count, std::chrono::milliseconds default_debounce)
    : default_debounce_(default_debounce) {
  if (worker_count == 0) {
    unsigned hw = std::thread::hardware_concurrency();
    if (hw < 2) hw = 2;
    if (hw > 8) hw = 8;
    worker_count = hw;
  }
  workers_.reserve(worker_count);
  for (size_t i = 0; i < worker_count; ++i) {
    workers_.emplace_back([this]() { worker_loop(); });
  }
}

Scheduler::~Scheduler() {
  shutdown();
}

std::shared_ptr<CancelToken> Scheduler::schedule(const std::string& key,
                                                 TaskFn fn,
                                                 TaskPriority priority,
                                                 std::chrono::milliseconds debounce) {
  if (debounce.count() < 0) {
    debounce = default_debounce_;
  }
  auto due = (debounce.count() == 0) ? Clock::time_point::min() : (Clock::now() + debounce);
  auto token = std::make_shared<CancelToken>();

  {
    std::lock_guard<std::mutex> lock(mu_);
    if (stopping_) {
      token->cancel();
      return token;
    }
    auto& entry = keys_[key];
    ++entry.generation;
    if (entry.pending_cancel) {
      entry.pending_cancel->cancel();
    }
    if (entry.running_cancel) {
      entry.running_cancel->cancel();
    }
    entry.has_pending = true;
    entry.priority = priority;
    entry.due_at = due;
    entry.seq = ++seq_counter_;
    entry.fn = std::move(fn);
    entry.pending_cancel = token;
  }
  cv_.notify_all();
  return token;
}

void Scheduler::cancel(const std::string& key) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = keys_.find(key);
  if (it == keys_.end()) return;
  auto& entry = it->second;
  if (entry.pending_cancel) {
    entry.pending_cancel->cancel();
    entry.pending_cancel.reset();
  }
  if (entry.running_cancel) {
    entry.running_cancel->cancel();
  }
  entry.has_pending = false;
  entry.fn = nullptr;
  idle_cv_.notify_all();
}

void Scheduler::remove_key(const std::string& key) {
  std::unique_lock<std::mutex> lock(mu_);
  auto it = keys_.find(key);
  if (it == keys_.end()) return;
  if (it->second.pending_cancel) it->second.pending_cancel->cancel();
  if (it->second.running_cancel) it->second.running_cancel->cancel();
  it->second.has_pending = false;
  it->second.fn = nullptr;
  idle_cv_.wait(lock, [&]() {
    auto cur = keys_.find(key);
    return cur == keys_.end() || !cur->second.is_running;
  });
  keys_.erase(key);
  idle_cv_.notify_all();
}

void Scheduler::wait_for(const std::string& key) {
  std::unique_lock<std::mutex> lock(mu_);
  idle_cv_.wait(lock, [&]() {
    auto it = keys_.find(key);
    if (it == keys_.end()) return true;
    return !it->second.has_pending && !it->second.is_running;
  });
}

void Scheduler::wait_idle() {
  std::unique_lock<std::mutex> lock(mu_);
  idle_cv_.wait(lock, [&]() {
    for (const auto& [k, entry] : keys_) {
      if (entry.has_pending || entry.is_running) return false;
    }
    return true;
  });
}

void Scheduler::shutdown() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (stopping_) return;
    stopping_ = true;
    for (auto& [k, entry] : keys_) {
      if (entry.pending_cancel) entry.pending_cancel->cancel();
      if (entry.running_cancel) entry.running_cancel->cancel();
      entry.has_pending = false;
      entry.fn = nullptr;
    }
  }
  cv_.notify_all();
  for (auto& t : workers_) {
    if (t.joinable()) t.join();
  }
  workers_.clear();
}

bool Scheduler::has_ready_or_stopping_locked(Clock::time_point now) const {
  if (stopping_) return true;
  for (const auto& [k, entry] : keys_) {
    if (entry.has_pending && !entry.is_running && entry.due_at <= now) {
      return true;
    }
  }
  return false;
}

std::string Scheduler::pick_next_ready_locked(Clock::time_point now) {
  std::string best_key;
  const KeyEntry* best = nullptr;
  for (const auto& [k, entry] : keys_) {
    if (!entry.has_pending || entry.is_running || entry.due_at > now) continue;
    if (!best) {
      best = &entry;
      best_key = k;
      continue;
    }
    if (static_cast<int>(entry.priority) > static_cast<int>(best->priority)) {
      best = &entry;
      best_key = k;
    } else if (entry.priority == best->priority && entry.seq < best->seq) {
      best = &entry;
      best_key = k;
    }
  }
  return best_key;
}

Scheduler::Clock::time_point Scheduler::earliest_due_locked() const {
  Clock::time_point earliest = Clock::time_point::max();
  for (const auto& [k, entry] : keys_) {
    if (entry.has_pending && !entry.is_running && entry.due_at < earliest) {
      earliest = entry.due_at;
    }
  }
  return earliest;
}

void Scheduler::worker_loop() {
  while (true) {
    TaskFn fn;
    std::shared_ptr<CancelToken> token;
    std::string key;

    {
      std::unique_lock<std::mutex> lock(mu_);
      while (true) {
        if (stopping_) return;
        auto now = Clock::now();
        key = pick_next_ready_locked(now);
        if (!key.empty()) {
          auto& entry = keys_[key];
          fn = std::move(entry.fn);
          token = std::move(entry.pending_cancel);
          entry.has_pending = false;
          entry.is_running = true;
          entry.running_cancel = token;
          break;
        }
        auto earliest = earliest_due_locked();
        if (earliest == Clock::time_point::max()) {
          cv_.wait(lock);
        } else {
          cv_.wait_until(lock, earliest);
        }
      }
    }

    if (fn && (!token || !token->is_cancelled())) {
      fn(token);
    }

    {
      std::lock_guard<std::mutex> lock(mu_);
      auto it = keys_.find(key);
      if (it != keys_.end()) {
        it->second.is_running = false;
        it->second.running_cancel.reset();
      }
    }
    cv_.notify_all();
    idle_cv_.notify_all();
  }
}

}  // namespace lx::host
