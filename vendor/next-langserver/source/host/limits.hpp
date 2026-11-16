#pragma once

#include <chrono>
#include <cstdint>

namespace lx::host {

/**
 * Host operational limits and tuning parameters (§9.5, §10.2, §10.3, §10.4).
 */
struct HostConfig {
  uint32_t max_file_bytes{64u * 1024u * 1024u};
  uint32_t max_tree_depth{1024};
  uint32_t max_nodes{2'000'000};
  uint32_t max_parse_ms{5000};
  uint32_t max_errors{256};

  uint32_t max_layer_depth{4};          // §9.5
  uint32_t history_snapshots_k{2};      // §10.3 (keep latest + K previous)
  std::chrono::milliseconds debounce{30};  // §10.2
  double full_reparse_ratio{0.5};       // §4.4 / §6.7
  uint32_t max_module_crashes{3};       // §14
  size_t scheduler_workers{0};          // 0 = auto
};

}  // namespace lx::host
