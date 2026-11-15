#include "host/lsp/semantic_tokens_delta.hpp"

#include <algorithm>
#include <cassert>

namespace lx::host::lsp {

std::vector<SemanticTokensEdit> compute_semantic_tokens_delta(
    std::span<const uint32_t> prev,
    std::span<const uint32_t> curr) {
  const size_t pn = prev.size();
  const size_t cn = curr.size();

  size_t prefix = 0;
  const size_t min_len = std::min(pn, cn);
  while (prefix < min_len && prev[prefix] == curr[prefix]) {
    ++prefix;
  }

  if (prefix == pn && prefix == cn) {
    return {};
  }

  size_t suffix = 0;
  while (prefix + suffix < pn &&
         prefix + suffix < cn &&
         prev[pn - 1 - suffix] == curr[cn - 1 - suffix]) {
    ++suffix;
  }

  const size_t prev_mid = pn - prefix - suffix;
  const size_t curr_mid = cn - prefix - suffix;

  // If both middle spans are sizable, look for a substantial identical interior run
  // (e.g. unchanged tokens between two separate edits) so we emit two small edits
  // instead of retransmitting everything in between.
  if (prev_mid >= 20 && curr_mid >= 20) {
    constexpr size_t kMinMatch = 15; // 3 full semantic tokens
    // Sample anchor at the middle of prev_mid and search in curr_mid.
    const size_t p_anchor = prefix + prev_mid / 2;
    if (p_anchor + kMinMatch <= pn - suffix) {
      const auto* needle = prev.data() + p_anchor;
      const auto* hay_begin = curr.data() + prefix;
      const auto* hay_end = curr.data() + (cn - suffix);
      const auto* found = std::search(hay_begin, hay_end, needle, needle + kMinMatch);
      if (found != hay_end) {
        size_t c_anchor = static_cast<size_t>(found - curr.data());
        // Expand match left & right as far as possible within middle spans.
        size_t p_l = p_anchor;
        size_t c_l = c_anchor;
        while (p_l > prefix && c_l > prefix && prev[p_l - 1] == curr[c_l - 1]) {
          --p_l;
          --c_l;
        }
        size_t p_r = p_anchor + kMinMatch;
        size_t c_r = c_anchor + kMinMatch;
        while (p_r < pn - suffix && c_r < cn - suffix && prev[p_r] == curr[c_r]) {
          ++p_r;
          ++c_r;
        }

        // Emit edit for [prefix, p_l) -> curr[prefix, c_l) and [p_r, pn - suffix) -> curr[c_r, cn - suffix).
        // LSP applies edits sequentially or in reverse-order; emitting in reverse order
        // (highest `start` first) ensures earlier offsets in `prev` remain valid, OR
        // notice LSP spec says `SemanticTokensEdit.start` refers to the ORIGINAL `prev`
        // array when edits are non-overlapping! Emitting in ascending `start` order with
        // non-overlapping ranges (or reverse? Wait: in LSP spec, clients apply edits from
        // end to start OR adjust offsets; VSCode expects non-overlapping `start` in original
        // indices and sorts/applies safely, while our `apply_semantic_tokens_edits` supports
        // original indices!). Let's verify: if we emit ascending original indices, let's be careful:
        // Only split if the common interior match saves >= kMinMatch elements.
        if (p_r - p_l >= kMinMatch) {
          std::vector<SemanticTokensEdit> edits;
          if (p_l > prefix || c_l > prefix) {
            SemanticTokensEdit e1;
            e1.start = static_cast<uint32_t>(prefix);
            e1.delete_count = static_cast<uint32_t>(p_l - prefix);
            e1.data.assign(curr.begin() + static_cast<ptrdiff_t>(prefix),
                           curr.begin() + static_cast<ptrdiff_t>(c_l));
            edits.push_back(std::move(e1));
          }
          if (p_r < pn - suffix || c_r < cn - suffix) {
            SemanticTokensEdit e2;
            e2.start = static_cast<uint32_t>(p_r);
            e2.delete_count = static_cast<uint32_t>((pn - suffix) - p_r);
            e2.data.assign(curr.begin() + static_cast<ptrdiff_t>(c_r),
                           curr.begin() + static_cast<ptrdiff_t>(cn - suffix));
            edits.push_back(std::move(e2));
          }
          return edits;
        }
      }
    }
  }

  SemanticTokensEdit edit;
  edit.start = static_cast<uint32_t>(prefix);
  edit.delete_count = static_cast<uint32_t>(prev_mid);
  edit.data.assign(curr.begin() + static_cast<ptrdiff_t>(prefix),
                   curr.begin() + static_cast<ptrdiff_t>(cn - suffix));
  return {std::move(edit)};
}

std::vector<uint32_t> apply_semantic_tokens_edits(
    std::span<const uint32_t> prev,
    std::span<const SemanticTokensEdit> edits) {
  std::vector<uint32_t> out;
  out.reserve(prev.size());
  size_t cursor = 0;

  std::vector<const SemanticTokensEdit*> sorted;
  sorted.reserve(edits.size());
  for (const auto& e : edits) {
    sorted.push_back(&e);
  }
  std::sort(sorted.begin(), sorted.end(), [](const SemanticTokensEdit* a, const SemanticTokensEdit* b) {
    return a->start < b->start;
  });

  for (const SemanticTokensEdit* e : sorted) {
    assert(e->start >= cursor && e->start + e->delete_count <= prev.size());
    out.insert(out.end(),
               prev.begin() + static_cast<ptrdiff_t>(cursor),
               prev.begin() + static_cast<ptrdiff_t>(e->start));
    out.insert(out.end(), e->data.begin(), e->data.end());
    cursor = static_cast<size_t>(e->start) + static_cast<size_t>(e->delete_count);
  }
  if (cursor < prev.size()) {
    out.insert(out.end(), prev.begin() + static_cast<ptrdiff_t>(cursor), prev.end());
  }
  return out;
}

std::string SemanticTokensCache::store(const std::string& uri, std::vector<uint32_t> data) {
  std::lock_guard<std::mutex> lock(mu_);
  std::string id = std::to_string(next_id_++);
  auto& q = by_uri_[uri];
  q.push_back(Entry{id, std::move(data)});
  while (q.size() > kMaxVersionsPerUri) {
    q.pop_front();
  }
  return id;
}

std::optional<std::vector<uint32_t>> SemanticTokensCache::find(
    const std::string& uri,
    std::string_view result_id) const {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = by_uri_.find(uri);
  if (it == by_uri_.end()) {
    return std::nullopt;
  }
  for (auto rit = it->second.rbegin(); rit != it->second.rend(); ++rit) {
    if (rit->result_id == result_id) {
      return rit->data;
    }
  }
  return std::nullopt;
}

void SemanticTokensCache::erase_uri(const std::string& uri) {
  std::lock_guard<std::mutex> lock(mu_);
  by_uri_.erase(uri);
}

} // namespace lx::host::lsp
