#include "engine/uast/uast.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace lx::uast {

namespace {

constexpr uint32_t kDiffFlagsMask =
    FLAG_LEAF | FLAG_HIDDEN | FLAG_ERROR | FLAG_MISSING | FLAG_EMBED |
    FLAG_RULE | FLAG_SUPERTYPE | FLAG_ALT_LABEL | FLAG_TRUNCATED;

struct StreamEntry {
  GreenRef ref;
  uint32_t byte_off = 0;
  uint32_t logical_idx = 0;
  uint32_t logical_len = 0;
};

class ForwardChildStream {
 public:
  struct Frame {
    const GreenInner* inner = nullptr;
    uint32_t idx = 0;
    uint32_t byte_off = 0;
    uint32_t logical_idx = 0;
    FieldId inherited_field = kNoField;
  };

  ForwardChildStream(const GreenInner* root, uint32_t base_byte_off) {
    if (root && root->child_count > 0) {
      stack_.push_back(Frame{root, 0, base_byte_off, 0, kNoField});
    }
  }

  bool at_end() const noexcept { return stack_.empty(); }

  StreamEntry peek() const noexcept {
    const Frame& f = stack_.back();
    const GreenRef& r = f.inner->child(f.idx);
    FieldId fid = r.field_id;
    if (fid == kNoField && f.inner->is_seq()) {
      fid = f.inherited_field;
    }
    const uint32_t llen = r.node->is_seq() ? r.node->as_inner()->logical_count : 1u;
    return StreamEntry{GreenRef{r.node, fid}, f.byte_off, f.logical_idx, llen};
  }

  void skip_current() noexcept {
    while (!stack_.empty()) {
      Frame& f = stack_.back();
      const GreenNode* c = f.inner->child(f.idx).node;
      const uint32_t w = c->width;
      const uint32_t ll = c->is_seq() ? c->as_inner()->logical_count : 1u;
      f.idx += 1;
      f.byte_off += w;
      f.logical_idx += ll;
      if (f.idx < f.inner->child_count) {
        return;
      }
      stack_.pop_back();
    }
  }

  void expand_seq() {
    const StreamEntry e = peek();
    if (!e.ref.node->is_seq()) return;
    const auto* seq = e.ref.node->as_inner();
    if (seq->child_count == 0) {
      skip_current();
      return;
    }
    stack_.push_back(Frame{seq, 0, e.byte_off, e.logical_idx, e.ref.field_id});
  }

 private:
  std::vector<Frame> stack_;
};

class BackwardChildStream {
 public:
  struct Frame {
    const GreenInner* inner = nullptr;
    uint32_t idx_plus_one = 0; // next child to inspect is idx_plus_one - 1
    uint32_t end_byte_off = 0;
    uint32_t end_logical_idx = 0;
    FieldId inherited_field = kNoField;
  };

  BackwardChildStream(const GreenInner* root, uint32_t base_byte_off) {
    if (root && root->child_count > 0) {
      stack_.push_back(Frame{root, root->child_count,
                             base_byte_off + root->width,
                             root->logical_count, kNoField});
    }
  }

  bool at_end(uint32_t min_logical_idx) const noexcept {
    return stack_.empty() || stack_.back().end_logical_idx <= min_logical_idx;
  }

  StreamEntry peek() const noexcept {
    const Frame& f = stack_.back();
    const GreenRef& r = f.inner->child(f.idx_plus_one - 1);
    FieldId fid = r.field_id;
    if (fid == kNoField && f.inner->is_seq()) {
      fid = f.inherited_field;
    }
    const uint32_t llen = r.node->is_seq() ? r.node->as_inner()->logical_count : 1u;
    return StreamEntry{GreenRef{r.node, fid}, f.end_byte_off - r.node->width,
                       f.end_logical_idx - llen, llen};
  }

  void skip_current() noexcept {
    while (!stack_.empty()) {
      Frame& f = stack_.back();
      const GreenNode* c = f.inner->child(f.idx_plus_one - 1).node;
      const uint32_t w = c->width;
      const uint32_t ll = c->is_seq() ? c->as_inner()->logical_count : 1u;
      f.idx_plus_one -= 1;
      f.end_byte_off -= w;
      f.end_logical_idx -= ll;
      if (f.idx_plus_one > 0) {
        return;
      }
      stack_.pop_back();
    }
  }

  void expand_seq() {
    const StreamEntry e = peek();
    if (!e.ref.node->is_seq()) return;
    const auto* seq = e.ref.node->as_inner();
    if (seq->child_count == 0) {
      skip_current();
      return;
    }
    stack_.push_back(Frame{seq, seq->child_count,
                           e.byte_off + seq->width,
                           e.logical_idx + e.logical_len,
                           e.ref.field_id});
  }

 private:
  std::vector<Frame> stack_;
};

struct LogicalItem {
  GreenRef ref;
  uint32_t byte_off = 0;
};

void collect_logical_range(const GreenInner* root, uint32_t base_off,
                           uint32_t start_idx, uint32_t end_idx,
                           std::vector<LogicalItem>& out) {
  if (start_idx >= end_idx) return;
  ForwardChildStream stream(root, base_off);
  while (!stream.at_end()) {
    const StreamEntry e = stream.peek();
    const uint32_t e_end = e.logical_idx + e.logical_len;
    if (e_end <= start_idx) {
      stream.skip_current();
      continue;
    }
    if (e.logical_idx >= end_idx) {
      break;
    }
    if (e.ref.node->is_seq()) {
      stream.expand_seq();
      continue;
    }
    out.push_back(LogicalItem{e.ref, e.byte_off});
    stream.skip_current();
  }
}

bool tokens_equal(const GreenToken* ot, const GreenToken* nt) noexcept {
  return ot->kind == nt->kind &&
         (ot->flags & kDiffFlagsMask) == (nt->flags & kDiffFlagsMask) &&
         ot->width == nt->width &&
         ot->text_hash == nt->text_hash;
}

bool refs_match_fast(const GreenRef& a, const GreenRef& b) noexcept {
  if (a == b) return true;
  if (a.field_id != b.field_id || !a.node || !b.node) return false;
  if (a.node->is_token() && b.node->is_token()) {
    return tokens_equal(a.node->as_token(), b.node->as_token());
  }
  return false;
}

bool nodes_compatible_for_descent(const GreenNode* a, const GreenNode* b) noexcept {
  if (a->is_token() || b->is_token()) return false;
  if (a->kind != b->kind) return false;
  if ((a->flags & kDiffFlagsMask) != (b->flags & kDiffFlagsMask)) return false;
  if (a->as_inner()->prec != b->as_inner()->prec) return false;
  return true;
}

void diff_nodes(const GreenNode* old_node, uint32_t old_off,
                const GreenNode* new_node, uint32_t new_off,
                std::vector<Range>& out);

void diff_item_slices(std::span<const LogicalItem> old_items, uint32_t old_end_byte,
                      std::span<const LogicalItem> new_items, uint32_t new_start_byte,
                      uint32_t new_end_byte, std::vector<Range>& out) {
  const size_t m = old_items.size();
  const size_t n = new_items.size();
  (void)old_end_byte;

  if (m == 0 && n == 0) return;
  if (m == 0) {
    out.push_back(Range{new_start_byte, new_end_byte});
    return;
  }
  if (n == 0) {
    out.push_back(Range{new_start_byte, new_start_byte});
    return;
  }
  if (m == 1 && n == 1) {
    if (refs_match_fast(old_items[0].ref, new_items[0].ref)) return;
    if (old_items[0].ref.field_id == new_items[0].ref.field_id &&
        nodes_compatible_for_descent(old_items[0].ref.node, new_items[0].ref.node)) {
      diff_nodes(old_items[0].ref.node, old_items[0].byte_off,
                 new_items[0].ref.node, new_items[0].byte_off, out);
    } else {
      out.push_back(Range{new_items[0].byte_off,
                          new_items[0].byte_off + new_items[0].ref.node->width});
    }
    return;
  }

  // Exact LCS for small/medium slices; pointer+kind greedy alignment for very large slices.
  if (m * n <= 4096) {
    auto can_pair = [](const GreenRef& a, const GreenRef& b) noexcept {
      if (a == b) return true;
      if (a.field_id != b.field_id) return false;
      if (a.node->kind != b.node->kind) return false;
      if ((a.node->flags & kDiffFlagsMask) != (b.node->flags & kDiffFlagsMask)) return false;
      return true;
    };

    const size_t cols = n + 1;
    std::vector<uint32_t> dp((m + 1) * cols, 0);
    for (size_t i = m; i > 0; --i) {
      for (size_t j = n; j > 0; --j) {
        if (can_pair(old_items[i - 1].ref, new_items[j - 1].ref)) {
          dp[(i - 1) * cols + (j - 1)] = 1 + dp[i * cols + j];
        } else {
          dp[(i - 1) * cols + (j - 1)] =
              std::max(dp[i * cols + (j - 1)], dp[(i - 1) * cols + j]);
        }
      }
    }

    size_t i = 0;
    size_t j = 0;
    while (i < m || j < n) {
      if (i < m && j < n && can_pair(old_items[i].ref, new_items[j].ref) &&
          dp[i * cols + j] == 1 + dp[(i + 1) * cols + (j + 1)]) {
        diff_nodes(old_items[i].ref.node, old_items[i].byte_off,
                   new_items[j].ref.node, new_items[j].byte_off, out);
        ++i;
        ++j;
      } else if (j < n && (i == m || dp[i * cols + (j + 1)] >= dp[(i + 1) * cols + j])) {
        out.push_back(Range{new_items[j].byte_off,
                            new_items[j].byte_off + new_items[j].ref.node->width});
        ++j;
      } else {
        const uint32_t del_pos = (j < n) ? new_items[j].byte_off : new_end_byte;
        out.push_back(Range{del_pos, del_pos});
        ++i;
      }
    }
    return;
  }

  // Large slice fallback: anchor on identical GreenNode* pointers in new_items.
  std::unordered_map<const GreenNode*, size_t> old_pos_map;
  old_pos_map.reserve(m);
  for (size_t i = 0; i < m; ++i) {
    old_pos_map.try_emplace(old_items[i].ref.node, i);
  }

  size_t i = 0;
  size_t j = 0;
  while (i < m && j < n) {
    auto it = old_pos_map.find(new_items[j].ref.node);
    if (it != old_pos_map.end() && it->second >= i &&
        old_items[it->second].ref.field_id == new_items[j].ref.field_id) {
      if (it->second > i) {
        out.push_back(Range{new_items[j].byte_off, new_items[j].byte_off});
      }
      i = it->second + 1;
      ++j;
    } else {
      out.push_back(Range{new_items[j].byte_off,
                          new_items[j].byte_off + new_items[j].ref.node->width});
      ++j;
    }
  }
  if (j < n) {
    out.push_back(Range{new_items[j].byte_off, new_end_byte});
  } else if (i < m) {
    out.push_back(Range{new_end_byte, new_end_byte});
  }
}

void diff_nodes(const GreenNode* old_node, uint32_t old_off,
                const GreenNode* new_node, uint32_t new_off,
                std::vector<Range>& out) {
  if (old_node == new_node) return;
  if (!new_node) return;
  if (!old_node) {
    out.push_back(Range{new_off, new_off + new_node->width});
    return;
  }

  if (old_node->is_token() && new_node->is_token()) {
    const auto* ot = old_node->as_token();
    const auto* nt = new_node->as_token();
    if (tokens_equal(ot, nt)) {
      return;
    }
    out.push_back(Range{new_off, new_off + nt->width});
    return;
  }

  if (!nodes_compatible_for_descent(old_node, new_node)) {
    out.push_back(Range{new_off, new_off + new_node->width});
    return;
  }

  const auto* oi = old_node->as_inner();
  const auto* ni = new_node->as_inner();

  // 1. Common prefix skipping (including entire shared Seq subtrees in O(1))
  ForwardChildStream fwd_old(oi, old_off);
  ForwardChildStream fwd_new(ni, new_off);

  uint32_t fwd_old_idx = 0;
  uint32_t fwd_new_idx = 0;
  uint32_t fwd_old_byte = old_off;
  uint32_t fwd_new_byte = new_off;

  while (!fwd_old.at_end() && !fwd_new.at_end()) {
    const StreamEntry eo = fwd_old.peek();
    const StreamEntry en = fwd_new.peek();
    if (refs_match_fast(eo.ref, en.ref)) {
      fwd_old_idx = eo.logical_idx + eo.logical_len;
      fwd_new_idx = en.logical_idx + en.logical_len;
      fwd_old_byte = eo.byte_off + eo.ref.node->width;
      fwd_new_byte = en.byte_off + en.ref.node->width;
      fwd_old.skip_current();
      fwd_new.skip_current();
      continue;
    }

    const bool o_seq = eo.ref.node->is_seq();
    const bool n_seq = en.ref.node->is_seq();
    if (o_seq && n_seq) {
      if (eo.ref.node->as_inner()->seq_height >= en.ref.node->as_inner()->seq_height) {
        fwd_old.expand_seq();
      } else {
        fwd_new.expand_seq();
      }
      continue;
    }
    if (o_seq) {
      fwd_old.expand_seq();
      continue;
    }
    if (n_seq) {
      fwd_new.expand_seq();
      continue;
    }
    break;
  }

  if (fwd_old.at_end() && fwd_new.at_end()) {
    return;
  }

  // 2. Common suffix skipping (including shared Seq subtrees in O(1))
  BackwardChildStream rev_old(oi, old_off);
  BackwardChildStream rev_new(ni, new_off);

  uint32_t rev_old_idx = oi->logical_count;
  uint32_t rev_new_idx = ni->logical_count;
  uint32_t rev_old_byte = old_off + oi->width;
  uint32_t rev_new_byte = new_off + ni->width;

  while (!rev_old.at_end(fwd_old_idx) && !rev_new.at_end(fwd_new_idx)) {
    const StreamEntry eo = rev_old.peek();
    const StreamEntry en = rev_new.peek();

    const bool crosses_fwd = (eo.logical_idx < fwd_old_idx) || (en.logical_idx < fwd_new_idx);
    if (!crosses_fwd && refs_match_fast(eo.ref, en.ref)) {
      rev_old_idx = eo.logical_idx;
      rev_new_idx = en.logical_idx;
      rev_old_byte = eo.byte_off;
      rev_new_byte = en.byte_off;
      rev_old.skip_current();
      rev_new.skip_current();
      continue;
    }

    const bool o_seq = eo.ref.node->is_seq();
    const bool n_seq = en.ref.node->is_seq();
    if (o_seq && n_seq) {
      if (eo.ref.node->as_inner()->seq_height >= en.ref.node->as_inner()->seq_height) {
        rev_old.expand_seq();
      } else {
        rev_new.expand_seq();
      }
      continue;
    }
    if (o_seq) {
      rev_old.expand_seq();
      continue;
    }
    if (n_seq) {
      rev_new.expand_seq();
      continue;
    }
    break;
  }

  // 3. Collect and diff only the unshared middle slice [fwd .. rev)
  std::vector<LogicalItem> old_mid;
  std::vector<LogicalItem> new_mid;
  collect_logical_range(oi, old_off, fwd_old_idx, rev_old_idx, old_mid);
  collect_logical_range(ni, new_off, fwd_new_idx, rev_new_idx, new_mid);

  (void)fwd_old_byte;
  diff_item_slices(old_mid, rev_old_byte, new_mid, fwd_new_byte, rev_new_byte, out);
}

} // namespace

std::vector<Range> tree_diff(const GreenNode* old_root, const GreenNode* new_root) {
  std::vector<Range> ranges;
  if (old_root == new_root) return ranges;

  if (!old_root && new_root) {
    ranges.push_back(Range{0, new_root->width});
    return ranges;
  }
  if (old_root && !new_root) {
    ranges.push_back(Range{0, 0});
    return ranges;
  }
  if (!old_root && !new_root) {
    return ranges;
  }

  diff_nodes(old_root, 0, new_root, 0, ranges);

  if (ranges.size() <= 1) return ranges;

  std::sort(ranges.begin(), ranges.end(), [](const Range& a, const Range& b) {
    if (a.start != b.start) return a.start < b.start;
    return a.end < b.end;
  });

  std::vector<Range> merged;
  merged.reserve(ranges.size());
  merged.push_back(ranges[0]);
  for (size_t i = 1; i < ranges.size(); ++i) {
    Range& back = merged.back();
    const Range& cur = ranges[i];
    if (cur.start <= back.end) {
      if (cur.end > back.end) {
        back.end = cur.end;
      }
    } else {
      merged.push_back(cur);
    }
  }
  return merged;
}

} // namespace lx::uast
