#include "engine/uast/uast.h"
#include "engine/uast/uast_internal.h"

#include <algorithm>
#include <array>
#include <cassert>

namespace lx::uast {

namespace {

GreenPtr make_seq_node(std::span<const GreenRef> children, uint32_t height,
                       uint32_t ctx_depth = 0, uint32_t ctx_hash = 0,
                       uint32_t pred_deps_hash = 0,
                       uint32_t extra_reach_ahead = 0) {
  return internal::allocate_inner_raw(
      kSeqKind, FLAG_SEQ, height, children,
      ctx_depth, ctx_hash, 0, pred_deps_hash, extra_reach_ahead);
}

// Wraps a single non-Seq node into a 1-element height-1 Seq.
GreenPtr wrap_as_seq(GreenPtr node) {
  if (!node) return nullptr;
  if (node->is_seq()) return node;
  GreenRef ref{node.get(), kNoField};
  return make_seq_node(std::span<const GreenRef>(&ref, 1), 1);
}

// Computes extra_reach_ahead that belongs strictly to root (above children's reach).
uint32_t compute_tail_extra_reach(const GreenInner* seq) noexcept {
  if (!seq) return 0;
  uint32_t child_max = 0;
  uint32_t suffix_w = 0;
  const auto ch = seq->children();
  for (size_t i = ch.size(); i > 0; --i) {
    const GreenNode* c = ch[i - 1].node;
    if (c->reach_ahead > suffix_w) {
      const uint32_t r = c->reach_ahead - suffix_w;
      if (r > child_max) child_max = r;
    }
    suffix_w += c->width;
  }
  return seq->reach_ahead > child_max ? seq->reach_ahead : 0;
}

// Splits a combined list of (1..32) children at `height` into 1 or 2 valid nodes.
// When 2 nodes are returned, each has 8..16 children.
std::pair<GreenPtr, GreenPtr> make_1_or_2_seq(std::span<const GreenRef> items,
                                              uint32_t height) {
  const size_t n = items.size();
  assert(n >= 1 && n <= kSeqMaxFanout * 2);
  if (n <= kSeqMaxFanout) {
    return {make_seq_node(items, height), nullptr};
  }
  const size_t left_n = (n + 1) / 2; // ceil(n / 2), in [9..16]
  const size_t right_n = n - left_n; // in [8..16]
  return {make_seq_node(items.subspan(0, left_n), height),
          make_seq_node(items.subspan(left_n, right_n), height)};
}

// Inserts R (height h_R) into right spine of L (height h_L > h_R).
// Returns 1 or 2 nodes at height h_L (if 2, each has 8..16 children).
std::pair<GreenPtr, GreenPtr> concat_into_right(const GreenInner* L,
                                                const GreenInner* R) {
  assert(L->seq_height > R->seq_height);
  const auto l_ch = L->children();

  if (L->seq_height == R->seq_height + 1) {
    // R's height equals L's children's height.
    if (R->child_count >= kSeqMinFanout) {
      // R is already valid as a non-root child of L!
      std::array<GreenRef, kSeqMaxFanout + 1> buf{};
      for (size_t i = 0; i < l_ch.size(); ++i) buf[i] = l_ch[i];
      buf[l_ch.size()] = GreenRef{R, kNoField};
      return make_1_or_2_seq({buf.data(), l_ch.size() + 1}, L->seq_height);
    }
    // R has < 8 children: merge L's last child + R's children (total 9..23).
    const auto* last_c = l_ch.back().node->as_inner();
    std::array<GreenRef, kSeqMaxFanout * 2> merged{};
    size_t m = 0;
    for (const auto& r : last_c->children()) merged[m++] = r;
    for (const auto& r : R->children()) merged[m++] = r;
    auto [n1, n2] = make_1_or_2_seq({merged.data(), m}, R->seq_height);

    std::array<GreenRef, kSeqMaxFanout + 1> buf{};
    size_t k = 0;
    for (size_t i = 0; i + 1 < l_ch.size(); ++i) buf[k++] = l_ch[i];
    buf[k++] = GreenRef{n1.get(), kNoField};
    if (n2) buf[k++] = GreenRef{n2.get(), kNoField};
    return make_1_or_2_seq({buf.data(), k}, L->seq_height);
  }

  // Descend into L's last child
  const auto* last_c = l_ch.back().node->as_inner();
  auto [n1, n2] = concat_into_right(last_c, R);

  std::array<GreenRef, kSeqMaxFanout + 1> buf{};
  size_t k = 0;
  for (size_t i = 0; i + 1 < l_ch.size(); ++i) buf[k++] = l_ch[i];
  buf[k++] = GreenRef{n1.get(), kNoField};
  if (n2) buf[k++] = GreenRef{n2.get(), kNoField};
  return make_1_or_2_seq({buf.data(), k}, L->seq_height);
}

// Inserts L (height h_L) into left spine of R (height h_R > h_L).
std::pair<GreenPtr, GreenPtr> concat_into_left(const GreenInner* L,
                                               const GreenInner* R) {
  assert(R->seq_height > L->seq_height);
  const auto r_ch = R->children();

  if (R->seq_height == L->seq_height + 1) {
    if (L->child_count >= kSeqMinFanout) {
      std::array<GreenRef, kSeqMaxFanout + 1> buf{};
      buf[0] = GreenRef{L, kNoField};
      for (size_t i = 0; i < r_ch.size(); ++i) buf[i + 1] = r_ch[i];
      return make_1_or_2_seq({buf.data(), r_ch.size() + 1}, R->seq_height);
    }
    const auto* first_c = r_ch.front().node->as_inner();
    std::array<GreenRef, kSeqMaxFanout * 2> merged{};
    size_t m = 0;
    for (const auto& r : L->children()) merged[m++] = r;
    for (const auto& r : first_c->children()) merged[m++] = r;
    auto [n1, n2] = make_1_or_2_seq({merged.data(), m}, L->seq_height);

    std::array<GreenRef, kSeqMaxFanout + 1> buf{};
    size_t k = 0;
    buf[k++] = GreenRef{n1.get(), kNoField};
    if (n2) buf[k++] = GreenRef{n2.get(), kNoField};
    for (size_t i = 1; i < r_ch.size(); ++i) buf[k++] = r_ch[i];
    return make_1_or_2_seq({buf.data(), k}, R->seq_height);
  }

  const auto* first_c = r_ch.front().node->as_inner();
  auto [n1, n2] = concat_into_left(L, first_c);

  std::array<GreenRef, kSeqMaxFanout + 1> buf{};
  size_t k = 0;
  buf[k++] = GreenRef{n1.get(), kNoField};
  if (n2) buf[k++] = GreenRef{n2.get(), kNoField};
  for (size_t i = 1; i < r_ch.size(); ++i) buf[k++] = r_ch[i];
  return make_1_or_2_seq({buf.data(), k}, R->seq_height);
}

std::pair<GreenPtr, GreenPtr> split_seq_rec(const GreenInner* node, uint32_t idx) {
  const auto ch = node->children();
  if (idx == 0) {
    return {nullptr, GreenPtr(node)};
  }
  if (idx >= node->logical_count) {
    return {GreenPtr(node), nullptr};
  }

  if (node->seq_height == 1) {
    GreenPtr left = make_seq_node(ch.subspan(0, idx), 1);
    GreenPtr right = make_seq_node(ch.subspan(idx), 1);
    return {std::move(left), std::move(right)};
  }

  uint32_t cur = 0;
  for (size_t i = 0; i < ch.size(); ++i) {
    const auto* child_seq = ch[i].node->as_inner();
    const uint32_t c_cnt = child_seq->logical_count;
    if (idx < cur + c_cnt) {
      const uint32_t rel = idx - cur;
      auto [sub_l, sub_r] = split_seq_rec(child_seq, rel);

      GreenPtr left_prefix;
      if (i == 1) {
        left_prefix = GreenPtr(ch[0].node);
      } else if (i > 1) {
        left_prefix = make_seq_node(ch.subspan(0, i), node->seq_height);
      }
      GreenPtr left = seq_concat(std::move(left_prefix), std::move(sub_l));

      GreenPtr right_suffix;
      const size_t rem = ch.size() - (i + 1);
      if (rem == 1) {
        right_suffix = GreenPtr(ch[i + 1].node);
      } else if (rem > 1) {
        right_suffix = make_seq_node(ch.subspan(i + 1), node->seq_height);
      }
      GreenPtr right = seq_concat(std::move(sub_r), std::move(right_suffix));
      return {std::move(left), std::move(right)};
    }
    cur += c_cnt;
  }
  return {GreenPtr(node), nullptr};
}

// Finds logical item split index for byte_offset:
// all items with end <= byte_offset (and not starting at byte_offset if zero-width)
// go to the left; returns {split_index, split_byte}.
std::pair<uint32_t, uint32_t> find_byte_split_index(const GreenInner* node,
                                                    uint32_t byte_offset) noexcept {
  uint32_t idx_acc = 0;
  uint32_t byte_acc = 0;
  const GreenInner* cur = node;

  while (cur->seq_height > 1) {
    bool descended = false;
    for (const GreenRef& r : cur->children()) {
      const auto* c = r.node->as_inner();
      const uint32_t end = byte_acc + c->width;
      if (byte_offset < end) {
        cur = c;
        descended = true;
        break;
      }
      idx_acc += c->logical_count;
      byte_acc = end;
    }
    if (!descended) {
      return {idx_acc, byte_acc};
    }
  }

  // Height 1: children are logical items
  for (const GreenRef& r : cur->children()) {
    const uint32_t w = r.node->width;
    if (byte_acc == byte_offset) {
      return {idx_acc, byte_acc};
    }
    if (byte_acc + w > byte_offset) {
      return {idx_acc, byte_acc};
    }
    idx_acc += 1;
    byte_acc += w;
  }
  return {idx_acc, byte_acc};
}

// Finds logical index of the first item whose reach overlaps dirty (or the last item
// if seq's own extra_reach_ahead overlaps dirty).
uint32_t find_first_dirty_index(const GreenInner* node, uint32_t offset,
                                std::span<const DirtySpan> dirty) noexcept {
  if (is_clean(offset, node->width, node->reach_ahead, dirty)) {
    return node->logical_count;
  }

  const auto ch = node->children();
  if (node->seq_height == 1) {
    uint32_t cur_off = offset;
    for (uint32_t i = 0; i < ch.size(); ++i) {
      const GreenNode* item = ch[i].node;
      if (!is_clean(offset, (cur_off - offset) + item->width, item->reach_ahead, dirty)) {
        return i;
      }
      cur_off += item->width;
    }
    // All items were individually clean, so node's own tail extra_reach_ahead hit dirty.
    // §6.4: reparse the last element.
    return node->logical_count > 0 ? node->logical_count - 1 : 0;
  }

  uint32_t idx_acc = 0;
  uint32_t cur_off = offset;
  for (const GreenRef& r : ch) {
    const auto* c = r.node->as_inner();
    if (!is_clean(offset, (cur_off - offset) + c->width, c->reach_ahead, dirty)) {
      return is_clean(cur_off, c->width, c->reach_ahead, dirty)
                 ? idx_acc
                 : (idx_acc + find_first_dirty_index(c, cur_off, dirty));
    }
    idx_acc += c->logical_count;
    cur_off += c->width;
  }
  return node->logical_count > 0 ? node->logical_count - 1 : 0;
}

} // namespace

GreenPtr seq_from_items(std::span<const GreenRef> items,
                        uint32_t ctx_depth, uint32_t ctx_hash,
                        uint32_t pred_deps_hash, uint32_t extra_reach_ahead) {
  if (items.empty()) return nullptr;

  std::vector<GreenRef> cur_level(items.begin(), items.end());
  std::vector<GreenPtr> owned_nodes;
  uint32_t height = 1;

  while (cur_level.size() > kSeqMaxFanout) {
    const size_t n = cur_level.size();
    const size_t num_groups = (n + kSeqMaxFanout - 1) / kSeqMaxFanout;
    const size_t base = n / num_groups;
    const size_t rem = n % num_groups;

    std::vector<GreenRef> next_level;
    next_level.reserve(num_groups);
    std::vector<GreenPtr> next_owned;
    next_owned.reserve(num_groups);

    size_t pos = 0;
    for (size_t g = 0; g < num_groups; ++g) {
      const size_t g_len = base + (g < rem ? 1 : 0);
      GreenPtr node = make_seq_node(
          std::span<const GreenRef>(cur_level.data() + pos, g_len), height);
      next_level.push_back(GreenRef{node.get(), kNoField});
      next_owned.push_back(std::move(node));
      pos += g_len;
    }

    cur_level = std::move(next_level);
    owned_nodes = std::move(next_owned);
    ++height;
  }

  return make_seq_node(cur_level, height, ctx_depth, ctx_hash,
                       pred_deps_hash, extra_reach_ahead);
}

GreenPtr seq_concat(GreenPtr left, GreenPtr right) {
  if (!left) return right;
  if (!right) return left;

  left = wrap_as_seq(std::move(left));
  right = wrap_as_seq(std::move(right));

  const auto* L = left->as_inner();
  const auto* R = right->as_inner();

  if (L->seq_height == R->seq_height) {
    std::array<GreenRef, kSeqMaxFanout * 2> combined{};
    size_t k = 0;
    for (const auto& r : L->children()) combined[k++] = r;
    for (const auto& r : R->children()) combined[k++] = r;
    auto [n1, n2] = make_1_or_2_seq({combined.data(), k}, L->seq_height);
    if (!n2) return std::move(n1);
    std::array<GreenRef, 2> roots{GreenRef{n1.get(), kNoField},
                                  GreenRef{n2.get(), kNoField}};
    return make_seq_node(roots, L->seq_height + 1);
  }

  if (L->seq_height > R->seq_height) {
    auto [n1, n2] = concat_into_right(L, R);
    if (!n2) return std::move(n1);
    std::array<GreenRef, 2> roots{GreenRef{n1.get(), kNoField},
                                  GreenRef{n2.get(), kNoField}};
    return make_seq_node(roots, L->seq_height + 1);
  }

  auto [n1, n2] = concat_into_left(L, R);
  if (!n2) return std::move(n1);
  std::array<GreenRef, 2> roots{GreenRef{n1.get(), kNoField},
                                GreenRef{n2.get(), kNoField}};
  return make_seq_node(roots, R->seq_height + 1);
}

SeqSplitResult seq_split_at_index(const GreenNode* seq, uint32_t index) {
  if (!seq) return {nullptr, nullptr, true, 0, 0};

  if (!seq->is_seq()) {
    if (index == 0) return {nullptr, wrap_as_seq(GreenPtr(seq)), true, 0, 0};
    return {wrap_as_seq(GreenPtr(seq)), nullptr, true, 1, seq->width};
  }

  const auto* root = seq->as_inner();
  const uint32_t total = root->logical_count;
  if (index == 0) {
    return {nullptr, GreenPtr(seq), true, 0, 0};
  }
  if (index >= total) {
    return {GreenPtr(seq), nullptr, true, total, root->width};
  }

  const uint32_t tail_extra = compute_tail_extra_reach(root);
  auto [l, r] = split_seq_rec(root, index);
  if (r && tail_extra > r->reach_ahead) {
    r = seq_with_context(r.get(), 0, 0, 0, tail_extra);
  }
  const uint32_t split_byte = l ? l->width : 0u;
  return {std::move(l), std::move(r), true, index, split_byte};
}

SeqSplitResult seq_split_at_byte(const GreenNode* seq, uint32_t byte_offset) {
  if (!seq) return {nullptr, nullptr, byte_offset == 0, 0, 0};
  if (byte_offset == 0) {
    return {nullptr, wrap_as_seq(GreenPtr(seq)), true, 0, 0};
  }
  if (byte_offset >= seq->width) {
    return {wrap_as_seq(GreenPtr(seq)), nullptr,
            byte_offset == seq->width, seq_count(seq), seq->width};
  }

  if (!seq->is_seq()) {
    return {nullptr, wrap_as_seq(GreenPtr(seq)), false, 0, 0};
  }

  const auto* root = seq->as_inner();
  auto [idx, split_b] = find_byte_split_index(root, byte_offset);
  SeqSplitResult res = seq_split_at_index(seq, idx);
  res.exact_boundary = (split_b == byte_offset);
  return res;
}

std::optional<GreenRef> seq_get(const GreenNode* seq, uint32_t index) {
  if (!seq) return std::nullopt;
  if (!seq->is_seq()) {
    if (index == 0) return GreenRef{seq, kNoField};
    return std::nullopt;
  }

  const auto* cur = seq->as_inner();
  if (index >= cur->logical_count) return std::nullopt;

  uint32_t rem = index;
  while (cur->seq_height > 1) {
    for (const GreenRef& r : cur->children()) {
      const auto* c = r.node->as_inner();
      if (rem < c->logical_count) {
        cur = c;
        break;
      }
      rem -= c->logical_count;
    }
  }
  return cur->child(rem);
}

std::optional<SeqItemHit> seq_find_by_byte(const GreenNode* seq, uint32_t byte_offset) {
  if (!seq) return std::nullopt;
  if (byte_offset >= seq->width && !(seq->width == 0 && byte_offset == 0)) {
    return std::nullopt;
  }
  if (!seq->is_seq()) {
    return SeqItemHit{GreenRef{seq, kNoField}, 0, 0, 0};
  }

  const auto* cur = seq->as_inner();
  uint32_t idx_acc = 0;
  uint32_t byte_acc = 0;
  uint32_t tok_acc = 0;

  while (cur->seq_height > 1) {
    bool found = false;
    for (const GreenRef& r : cur->children()) {
      const auto* c = r.node->as_inner();
      if (byte_offset < byte_acc + c->width || (c->width == 0 && byte_offset == byte_acc)) {
        cur = c;
        found = true;
        break;
      }
      idx_acc += c->logical_count;
      byte_acc += c->width;
      tok_acc += c->token_count;
    }
    if (!found) return std::nullopt;
  }

  for (uint32_t i = 0; i < cur->child_count; ++i) {
    const GreenRef& r = cur->child(i);
    if (byte_offset < byte_acc + r.node->width || (r.node->width == 0 && byte_offset == byte_acc)) {
      return SeqItemHit{r, idx_acc + i, byte_acc, tok_acc};
    }
    byte_acc += r.node->width;
    tok_acc += r.node->get_token_count();
  }
  return std::nullopt;
}

std::optional<SeqItemHit> seq_find_by_token(const GreenNode* seq, uint32_t token_index) {
  if (!seq || token_index >= seq->get_token_count()) return std::nullopt;
  if (!seq->is_seq()) {
    return SeqItemHit{GreenRef{seq, kNoField}, 0, 0, 0};
  }

  const auto* cur = seq->as_inner();
  uint32_t idx_acc = 0;
  uint32_t byte_acc = 0;
  uint32_t tok_acc = 0;

  while (cur->seq_height > 1) {
    bool found = false;
    for (const GreenRef& r : cur->children()) {
      const auto* c = r.node->as_inner();
      if (token_index < tok_acc + c->token_count) {
        cur = c;
        found = true;
        break;
      }
      idx_acc += c->logical_count;
      byte_acc += c->width;
      tok_acc += c->token_count;
    }
    if (!found) return std::nullopt;
  }

  for (uint32_t i = 0; i < cur->child_count; ++i) {
    const GreenRef& r = cur->child(i);
    const uint32_t tc = r.node->get_token_count();
    if (token_index < tok_acc + tc) {
      return SeqItemHit{r, idx_acc + i, byte_acc, tok_acc};
    }
    byte_acc += r.node->width;
    tok_acc += tc;
  }
  return std::nullopt;
}

GreenPtr seq_longest_clean_prefix(const GreenNode* seq, uint32_t seq_start_old,
                                  std::span<const DirtySpan> dirty) {
  if (!seq) return nullptr;
  if (dirty.empty()) return wrap_as_seq(GreenPtr(seq));

  if (!seq->is_seq()) {
    if (is_clean(seq_start_old, seq->width, seq->reach_ahead, dirty)) {
      return wrap_as_seq(GreenPtr(seq));
    }
    return nullptr;
  }

  const auto* root = seq->as_inner();
  if (is_clean(seq_start_old, root->width, root->reach_ahead, dirty)) {
    return GreenPtr(seq);
  }

  const uint32_t k = find_first_dirty_index(root, seq_start_old, dirty);
  if (k == 0) return nullptr;
  return seq_split_at_index(seq, k).left;
}

GreenPtr seq_clean_run_from(const GreenNode* seq, uint32_t seq_start_old,
                            uint32_t from_old_byte,
                            std::span<const DirtySpan> dirty) {
  if (!seq || from_old_byte < seq_start_old) return nullptr;
  const uint32_t rel = from_old_byte - seq_start_old;
  if (rel >= seq->width) return nullptr;

  SeqSplitResult split = seq_split_at_byte(seq, rel);
  if (!split.exact_boundary || !split.right) return nullptr;

  return seq_longest_clean_prefix(split.right.get(), from_old_byte, dirty);
}

GreenPtr seq_with_context(const GreenNode* seq, uint32_t ctx_depth, uint32_t ctx_hash,
                          uint32_t pred_deps_hash, uint32_t extra_reach_ahead) {
  if (!seq) return nullptr;
  if (!seq->is_seq()) {
    GreenRef ref{seq, kNoField};
    return make_seq_node(std::span<const GreenRef>(&ref, 1), 1,
                         ctx_depth, ctx_hash, pred_deps_hash, extra_reach_ahead);
  }
  const auto* s = seq->as_inner();
  return make_seq_node(s->children(), s->seq_height,
                       ctx_depth, ctx_hash, pred_deps_hash, extra_reach_ahead);
}

// ============================================================================
// Logical Equality (U-Seq Invariant §5.4)
// ============================================================================

namespace {

struct LogicalIter {
  struct Frame {
    const GreenInner* inner = nullptr;
    uint32_t idx = 0;
  };
  std::vector<Frame> stack;

  explicit LogicalIter(const GreenInner* root) {
    if (root) stack.push_back({root, 0});
  }

  bool next(GreenRef& out) {
    while (!stack.empty()) {
      Frame& top = stack.back();
      if (top.idx >= top.inner->child_count) {
        stack.pop_back();
        continue;
      }
      const GreenRef& ref = top.inner->child(top.idx++);
      if (ref.node && ref.node->is_seq()) {
        stack.push_back({ref.node->as_inner(), 0});
        continue;
      }
      out = ref;
      return true;
    }
    return false;
  }
};

} // namespace

bool logical_equal(const GreenNode* a, const GreenNode* b) noexcept {
  if (a == b) return true;
  if (!a || !b) return false;

  if (a->is_token() || b->is_token()) {
    if (!a->is_token() || !b->is_token()) return false;
    const auto* ta = a->as_token();
    const auto* tb = b->as_token();
    return ta->kind == tb->kind &&
           ta->width == tb->width &&
           (ta->flags & ~(FLAG_SEQ | FLAG_HAS_ERROR)) ==
               (tb->flags & ~(FLAG_SEQ | FLAG_HAS_ERROR)) &&
           ta->text_hash == tb->text_hash &&
           ta->lex_state_in == tb->lex_state_in &&
           ta->lex_state_out == tb->lex_state_out;
  }

  const auto* ia = a->as_inner();
  const auto* ib = b->as_inner();

  if (ia->is_seq() != ib->is_seq()) return false;
  if (ia->width != ib->width || ia->logical_count != ib->logical_count ||
      ia->struct_hash != ib->struct_hash) {
    return false;
  }
  if (!ia->is_seq()) {
    if (ia->kind != ib->kind || ia->prec != ib->prec ||
        (ia->flags & ~(FLAG_SEQ | FLAG_HAS_ERROR)) !=
            (ib->flags & ~(FLAG_SEQ | FLAG_HAS_ERROR))) {
      return false;
    }
  }

  LogicalIter it_a(ia);
  LogicalIter it_b(ib);
  GreenRef ra{}, rb{};
  while (true) {
    const bool has_a = it_a.next(ra);
    const bool has_b = it_b.next(rb);
    if (has_a != has_b) return false;
    if (!has_a) return true;
    if (ra.field_id != rb.field_id) return false;
    if (!logical_equal(ra.node, rb.node)) return false;
  }
}

} // namespace lx::uast
