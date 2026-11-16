#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace lx::host {

enum class LogLevel : int {
  Error = 0,
  Warn = 1,
  Info = 2,
  Debug = 3,
  Trace = 4,
};

struct TraceEvent {
  uint64_t timestamp_us{0};
  LogLevel level{LogLevel::Info};
  std::string category;   // e.g. "reparse", "lex", "reuse", "diff", "layer", "module"
  std::string doc_uri;
  std::string message;
};

struct HistogramStats {
  uint64_t count{0};
  double min_val{0.0};
  double max_val{0.0};
  double sum{0.0};
  double p50{0.0};
  double p99{0.0};
};

class Histogram {
 public:
  explicit Histogram(size_t max_samples = 4096) : max_samples_(max_samples) {}

  void record(double value);
  HistogramStats stats() const;
  void reset();

 private:
  size_t max_samples_;
  mutable std::mutex mu_;
  std::deque<double> samples_;
  uint64_t total_count_{0};
  double min_val_{0.0};
  double max_val_{0.0};
  double sum_{0.0};
};

/**
 * Host observability metrics per §12.2 and §14.
 */
struct HostMetrics {
  std::atomic<uint64_t> reparses_total{0};
  std::atomic<uint64_t> reparses_full{0};
  std::atomic<uint64_t> reparses_incremental{0};
  std::atomic<uint64_t> reparses_cancelled{0};
  std::atomic<uint64_t> tokens_relexed{0};
  std::atomic<uint64_t> nodes_reused{0};
  std::atomic<uint64_t> nodes_created{0};
  std::atomic<uint64_t> changed_ranges_count{0};
  std::atomic<uint64_t> changed_ranges_bytes{0};
  std::atomic<uint64_t> alloc_count{0};
  std::atomic<uint64_t> module_crashes{0};

  Histogram reparse_us;
  Histogram tree_diff_us;
  Histogram reuse_ratio;

  double average_reuse_ratio() const {
    uint64_t r = nodes_reused.load(std::memory_order_relaxed);
    uint64_t c = nodes_created.load(std::memory_order_relaxed);
    uint64_t total = r + c;
    return total > 0 ? static_cast<double>(r) / static_cast<double>(total) : 0.0;
  }

  void reset();
};

/**
 * Thread-safe trace & log sink with bounded ring buffer for inspecting
 * individual reparses (tokens, reuse decisions, refusal reasons) per §14.
 */
class TraceLog {
 public:
  using SinkFn = std::function<void(const TraceEvent&)>;

  explicit TraceLog(size_t max_events = 2048);

  void set_level(LogLevel level) { level_.store(static_cast<int>(level), std::memory_order_relaxed); }
  LogLevel level() const { return static_cast<LogLevel>(level_.load(std::memory_order_relaxed)); }

  void set_sink(SinkFn sink);

  void log(LogLevel lvl, std::string category, std::string doc_uri, std::string message);

  std::vector<TraceEvent> recent(size_t limit = 256) const;
  void clear();

 private:
  size_t max_events_;
  std::atomic<int> level_{static_cast<int>(LogLevel::Info)};
  mutable std::mutex mu_;
  std::deque<TraceEvent> events_;
  SinkFn sink_;
  std::chrono::steady_clock::time_point start_{std::chrono::steady_clock::now()};
};

}  // namespace lx::host
