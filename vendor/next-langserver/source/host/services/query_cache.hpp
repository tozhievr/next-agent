#pragma once

#include "host/query_engine.hpp"
#include "host/services.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lx::host::services {

/**
 * Metadata directive from `(#set! key "value")` inside a query pattern.
 */
struct QuerySetDirective {
  uint32_t pattern_index{0};
  std::string key;
  std::string value;
};

struct CachedQueryEntry {
  std::shared_ptr<const CompiledQuery> query;
  std::vector<QuerySetDirective> set_directives;
};

/**
 * Thread-safe cache for compiled S-expression queries.
 * Avoids re-parsing `.scm` query sources on every request / edit.
 */
class QueryCache {
 public:
  explicit QueryCache(size_t max_entries = 512) : max_entries_(max_entries) {}

  std::shared_ptr<const CachedQueryEntry> get_or_compile(
      std::string_view source,
      std::string* out_error = nullptr);

  void clear();
  size_t size() const noexcept;

  static QueryCache& global();

 private:
  size_t max_entries_;
  mutable std::shared_mutex mu_;
  std::unordered_map<std::string, std::shared_ptr<const CachedQueryEntry>> entries_;
};

/**
 * Extracts `(#set! key "value")` directives from a `.scm` query source
 * (keyed by pattern index).
 */
std::vector<QuerySetDirective> extract_set_directives(std::string_view source);

/**
 * Thread-safe incremental highlight cache keyed by `(module_epoch, root_green_ptr)`
 * and supporting incremental recomputation using `changed_ranges` (§5.8, §11).
 */
class HighlightCache {
 public:
  explicit HighlightCache(size_t max_entries = 128) : max_entries_(max_entries) {}

  // Returns cached spans if `(module_epoch, root)` was already highlighted for the
  // full document (no visible_range filter), or computes & caches them.
  std::vector<SemanticTokenSpan> get_or_compute(
      const Snapshot& snapshot,
      std::optional<lx::uast::Range> visible_range = std::nullopt);

  // Incrementally updates `prev_spans` given `new_snapshot` and `changed_ranges`
  // in `new_snapshot` coordinates (§11: recomputes only changed_ranges U visible_range).
  // Spans strictly outside changed_ranges (and shifted by edit deltas) can be reused
  // when `prev_change_set` is provided, or recomputed only inside changed_ranges.
  static std::vector<SemanticTokenSpan> highlight_incremental(
      const Snapshot& new_snapshot,
      const std::vector<SemanticTokenSpan>& prev_spans,
      const lx::ChangeSet& changes,
      std::span<const lx::uast::Range> changed_ranges,
      std::optional<lx::uast::Range> visible_range = std::nullopt);

  void clear();
  size_t size() const noexcept;

 private:
  struct CacheKey {
    uint64_t module_epoch{0};
    const lx::uast::GreenNode* root{nullptr};
    bool operator==(const CacheKey&) const noexcept = default;
  };
  struct CacheKeyHash {
    size_t operator()(const CacheKey& k) const noexcept {
      size_t h1 = std::hash<uint64_t>{}(k.module_epoch);
      size_t h2 = std::hash<const void*>{}(k.root);
      return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
    }
  };

  size_t max_entries_;
  mutable std::mutex mu_;
  std::unordered_map<CacheKey, std::vector<SemanticTokenSpan>, CacheKeyHash> cache_;
};

}  // namespace lx::host::services
