#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace lx::host::lsp {

/// Single LSP SemanticTokensEdit: replaces `prev[start .. start + delete_count)` with `data`.
struct SemanticTokensEdit {
  uint32_t start = 0;
  uint32_t delete_count = 0;
  std::vector<uint32_t> data;

  bool operator==(const SemanticTokensEdit&) const noexcept = default;
};

/// Computes minimal edit(s) transforming `prev` into `curr`.
/// Applying the returned edits in order to `prev` produces `curr` identically.
std::vector<SemanticTokensEdit> compute_semantic_tokens_delta(
    std::span<const uint32_t> prev,
    std::span<const uint32_t> curr);

/// Applies a sequence of SemanticTokensEdits to `prev` (utility for verification & tests).
std::vector<uint32_t> apply_semantic_tokens_edits(
    std::span<const uint32_t> prev,
    std::span<const SemanticTokensEdit> edits);

/// Thread-safe cache of recent encoded semantic tokens per document URI for delta requests.
class SemanticTokensCache {
 public:
  static constexpr size_t kMaxVersionsPerUri = 4;

  /// Records `data` for `uri` and returns a fresh monotonic `resultId`.
  std::string store(const std::string& uri, std::vector<uint32_t> data);

  /// Looks up cached tokens for `(uri, result_id)`. Returns std::nullopt if evicted or unknown.
  std::optional<std::vector<uint32_t>> find(const std::string& uri,
                                            std::string_view result_id) const;

  /// Drops all cached versions for `uri` (e.g. on textDocument/didClose).
  void erase_uri(const std::string& uri);

 private:
  struct Entry {
    std::string result_id;
    std::vector<uint32_t> data;
  };

  mutable std::mutex mu_;
  uint64_t next_id_ = 1;
  std::unordered_map<std::string, std::deque<Entry>> by_uri_;
};

} // namespace lx::host::lsp
