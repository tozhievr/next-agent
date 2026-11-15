#include "host/metrics.hpp"

#include <algorithm>
#include <cmath>

namespace lx::host {

void Histogram::record(double value) {
  std::lock_guard<std::mutex> lock(mu_);
  if (total_count_ == 0) {
    min_val_ = value;
    max_val_ = value;
  } else {
    min_val_ = std::min(min_val_, value);
    max_val_ = std::max(max_val_, value);
  }
  ++total_count_;
  sum_ += value;
  if (samples_.size() >= max_samples_) {
    samples_.pop_front();
  }
  samples_.push_back(value);
}

HistogramStats Histogram::stats() const {
  std::lock_guard<std::mutex> lock(mu_);
  HistogramStats s;
  s.count = total_count_;
  if (total_count_ == 0) return s;
  s.min_val = min_val_;
  s.max_val = max_val_;
  s.sum = sum_;
  if (!samples_.empty()) {
    std::vector<double> sorted(samples_.begin(), samples_.end());
    std::sort(sorted.begin(), sorted.end());
    auto pct = [&](double q) {
      size_t idx = static_cast<size_t>(std::ceil(q * static_cast<double>(sorted.size()))) ;
      if (idx == 0) idx = 1;
      if (idx > sorted.size()) idx = sorted.size();
      return sorted[idx - 1];
    };
    s.p50 = pct(0.50);
    s.p99 = pct(0.99);
  }
  return s;
}

void Histogram::reset() {
  std::lock_guard<std::mutex> lock(mu_);
  samples_.clear();
  total_count_ = 0;
  min_val_ = 0.0;
  max_val_ = 0.0;
  sum_ = 0.0;
}

void HostMetrics::reset() {
  reparses_total.store(0, std::memory_order_relaxed);
  reparses_full.store(0, std::memory_order_relaxed);
  reparses_incremental.store(0, std::memory_order_relaxed);
  reparses_cancelled.store(0, std::memory_order_relaxed);
  tokens_relexed.store(0, std::memory_order_relaxed);
  nodes_reused.store(0, std::memory_order_relaxed);
  nodes_created.store(0, std::memory_order_relaxed);
  changed_ranges_count.store(0, std::memory_order_relaxed);
  changed_ranges_bytes.store(0, std::memory_order_relaxed);
  alloc_count.store(0, std::memory_order_relaxed);
  module_crashes.store(0, std::memory_order_relaxed);
  reparse_us.reset();
  tree_diff_us.reset();
  reuse_ratio.reset();
}

TraceLog::TraceLog(size_t max_events) : max_events_(max_events) {}

void TraceLog::set_sink(SinkFn sink) {
  std::lock_guard<std::mutex> lock(mu_);
  sink_ = std::move(sink);
}

void TraceLog::log(LogLevel lvl, std::string category, std::string doc_uri, std::string message) {
  if (static_cast<int>(lvl) > level_.load(std::memory_order_relaxed)) {
    return;
  }
  auto now = std::chrono::steady_clock::now();
  uint64_t ts = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(now - start_).count());
  TraceEvent ev{ts, lvl, std::move(category), std::move(doc_uri), std::move(message)};

  SinkFn sink_copy;
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (events_.size() >= max_events_) {
      events_.pop_front();
    }
    events_.push_back(ev);
    sink_copy = sink_;
  }
  if (sink_copy) {
    sink_copy(ev);
  }
}

std::vector<TraceEvent> TraceLog::recent(size_t limit) const {
  std::lock_guard<std::mutex> lock(mu_);
  if (events_.size() <= limit) {
    return {events_.begin(), events_.end()};
  }
  return {events_.end() - static_cast<std::ptrdiff_t>(limit), events_.end()};
}

void TraceLog::clear() {
  std::lock_guard<std::mutex> lock(mu_);
  events_.clear();
}

}  // namespace lx::host
