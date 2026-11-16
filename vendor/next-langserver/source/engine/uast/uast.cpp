#include "engine/uast/uast.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <new>

namespace lx::uast {

// ============================================================================
// Allocator Hooks
// ============================================================================

namespace {

std::atomic<AllocFn> g_alloc_fn{nullptr};
std::atomic<FreeFn> g_free_fn{nullptr};

void destroy_single_node(const GreenNode* node) noexcept {
  if (!node) return;
  if (node->is_token()) {
    const auto* tok = static_cast<const GreenToken*>(node);
    tok->~GreenToken();
    uast_free(const_cast<GreenToken*>(tok));
  } else {
    const auto* inner = static_cast<const GreenInner*>(node);
    inner->~GreenInner();
    uast_free(const_cast<GreenInner*>(inner));
  }
}

void destroy_iterative(const GreenNode* root) noexcept {
  // Iterative worklist avoids C-stack overflow on arbitrarily deep trees.
  std::vector<const GreenNode*> worklist;
  worklist.push_back(root);

  while (!worklist.empty()) {
    const GreenNode* cur = worklist.back();
    worklist.pop_back();

    if (cur->is_inner()) {
      const auto* inner = static_cast<const GreenInner*>(cur);
      for (const GreenRef& ref : inner->children()) {
        if (ref.node && ref.node->rc.fetch_sub(1, std::memory_order_acq_rel) == 1) {
          worklist.push_back(ref.node);
        }
      }
    }
    destroy_single_node(cur);
  }
}

} // namespace

void set_allocator(AllocFn alloc_fn, FreeFn free_fn) noexcept {
  g_alloc_fn.store(alloc_fn, std::memory_order_release);
  g_free_fn.store(free_fn, std::memory_order_release);
}

void* uast_alloc(size_t size) {
  AllocFn fn = g_alloc_fn.load(std::memory_order_acquire);
  void* ptr = fn ? fn(size) : std::malloc(size);
  if (!ptr) throw std::bad_alloc();
  return ptr;
}

void uast_free(void* ptr) noexcept {
  if (!ptr) return;
  FreeFn fn = g_free_fn.load(std::memory_order_acquire);
  if (fn) {
    fn(ptr);
  } else {
    std::free(ptr);
  }
}

void GreenHeader::release() const noexcept {
  if (rc.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    destroy_iterative(this);
  }
}

// ============================================================================
// Reach Cleanliness (§5.5)
// ============================================================================

bool is_clean(uint32_t node_start, uint32_t node_width, uint32_t reach_ahead,
              std::span<const DirtySpan> dirty) noexcept {
  if (dirty.empty()) return true;
  const uint64_t x_start = node_start;
  const uint64_t x_end = static_cast<uint64_t>(node_start) + node_width + reach_ahead;

  // Binary search for first span with span.end > x_start, or check nearby.
  // Note: dirty spans are sorted and non-overlapping.
  size_t lo = 0;
  size_t hi = dirty.size();
  while (lo < hi) {
    const size_t mid = lo + (hi - lo) / 2;
    if (static_cast<uint64_t>(dirty[mid].start) < x_end) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  // All spans with index >= lo have span.start >= x_end, so they cannot overlap.
  // We need to check spans before lo (in reverse) while span.end > x_start or
  // since spans are non-overlapping, checking index lo - 1 is enough!
  // Wait: what if spans contain empty insertions [p, p)? Non-overlapping sorted spans:
  // If span[i].end <= x_start, could span[i-1].end > x_start? No, if sorted and non-overlapping!
  // Still, to be safe against adjacent empty spans at x_start, check backwards while start >= prev.
  if (lo > 0) {
    const size_t i = lo - 1;
    if (static_cast<uint64_t>(dirty[i].end) > x_start) {
      return false;
    }
    // Check one earlier in case dirty[i] was zero-width [p, p) with p == x_start
    if (i > 0 && static_cast<uint64_t>(dirty[i - 1].end) > x_start &&
        static_cast<uint64_t>(dirty[i - 1].start) < x_end) {
      return false;
    }
  }
  return true;
}

// ============================================================================
// Hashing & Structural Hash Helpers (U-Seq Monoid)
// ============================================================================

uint32_t fnv1a_32(const void* data, size_t len) noexcept {
  const auto* bytes = static_cast<const uint8_t*>(data);
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < len; ++i) {
    h ^= bytes[i];
    h *= 16777619u;
  }
  return h;
}

namespace internal {

constexpr uint32_t kPolyBase = 2654435761u; // odd -> invertible mod 2^32

uint32_t mix_u32(uint32_t h, uint32_t v) noexcept {
  h ^= v + 0x9e3779b9u + (h << 6) + (h >> 2);
  return h;
}

uint32_t pow_u32(uint32_t base, uint32_t exp) noexcept {
  uint32_t res = 1u;
  while (exp > 0) {
    if (exp & 1u) res *= base;
    base *= base;
    exp >>= 1u;
  }
  return res;
}

uint32_t hash_non_seq_ref(const GreenRef& ref) noexcept {
  uint32_t h = 0x811c9dc5u;
  h = mix_u32(h, static_cast<uint32_t>(ref.field_id));
  const GreenNode* n = ref.node;
  if (!n) return h;
  h = mix_u32(h, n->kind);
  h = mix_u32(h, n->width);
  h = mix_u32(h, n->flags & ~(FLAG_SEQ));
  if (n->is_token()) {
    const auto* tok = static_cast<const GreenToken*>(n);
    h = mix_u32(h, tok->text_hash);
    h = mix_u32(h, tok->lex_state_in);
    h = mix_u32(h, tok->lex_state_out);
  } else {
    const auto* inner = static_cast<const GreenInner*>(n);
    h = mix_u32(h, inner->struct_hash);
  }
  return h;
}

void compute_inner_metrics(GreenInner* out, std::span<const GreenRef> children,
                           uint32_t extra_reach_ahead) noexcept {
  uint32_t width = 0;
  uint32_t token_count = 0;
  uint32_t logical_count = 0;
  uint32_t children_poly = 0;
  bool any_error = (out->flags & (FLAG_ERROR | FLAG_MISSING | FLAG_HAS_ERROR)) != 0;

  for (const GreenRef& ref : children) {
    const GreenNode* c = ref.node;
    width += c->width;
    token_count += c->get_token_count();
    if (c->flags & (FLAG_ERROR | FLAG_MISSING | FLAG_HAS_ERROR)) {
      any_error = true;
    }

    if (c->is_seq()) {
      const auto* seq = static_cast<const GreenInner*>(c);
      logical_count += seq->logical_count;
      // Associative monoid combine: H = H * B^m + H_seq
      children_poly = children_poly * pow_u32(kPolyBase, seq->logical_count) + seq->struct_hash;
    } else {
      logical_count += 1;
      children_poly = children_poly * kPolyBase + hash_non_seq_ref(ref);
    }
  }

  // Compute reach_ahead (§5.5, I4):
  // max(extra_reach_ahead, max_i max(0, child_i.reach_ahead - suffix_width_after_i))
  uint32_t max_reach = extra_reach_ahead;
  uint32_t suffix_width = 0;
  for (size_t idx = children.size(); idx > 0; --idx) {
    const GreenNode* c = children[idx - 1].node;
    if (c->reach_ahead > suffix_width) {
      const uint32_t r = c->reach_ahead - suffix_width;
      if (r > max_reach) max_reach = r;
    }
    suffix_width += c->width;
  }

  out->width = width;
  out->reach_ahead = max_reach;
  out->token_count = token_count;
  out->logical_count = logical_count;
  if (any_error) {
    out->flags |= FLAG_HAS_ERROR;
  }

  if (out->is_seq()) {
    // U-Seq invariant: struct_hash depends ONLY on the logical sequence!
    out->struct_hash = children_poly;
  } else {
    uint32_t h = 0x517cc1b7u;
    h = mix_u32(h, out->kind);
    h = mix_u32(h, out->width);
    h = mix_u32(h, out->flags & ~(FLAG_SEQ | FLAG_HAS_ERROR));
    h = mix_u32(h, out->prec);
    h = mix_u32(h, logical_count);
    h = mix_u32(h, children_poly);
    out->struct_hash = h;
  }
}

GreenPtr allocate_token_raw(Kind kind, uint32_t width, uint32_t reach_ahead,
                            uint32_t flags, uint32_t text_hash,
                            uint32_t lex_state_in, uint32_t lex_state_out) {
  void* mem = uast_alloc(sizeof(GreenToken));
  auto* tok = new (mem) GreenToken();
  tok->rc.store(1, std::memory_order_relaxed);
  tok->kind = kind;
  tok->width = width;
  tok->reach_ahead = reach_ahead;
  tok->flags = flags | FLAG_LEAF;
  if (tok->flags & (FLAG_ERROR | FLAG_MISSING)) {
    tok->flags |= FLAG_HAS_ERROR;
  }
  tok->child_count = 0;
  tok->text_hash = text_hash;
  tok->lex_state_in = lex_state_in;
  tok->lex_state_out = lex_state_out;
  tok->_pad = 0;
  return GreenPtr::adopt(tok);
}

GreenPtr allocate_inner_raw(Kind kind, uint32_t flags, uint32_t seq_height,
                            std::span<const GreenRef> children,
                            uint32_t ctx_depth, uint32_t ctx_hash,
                            uint32_t prec, uint32_t pred_deps_hash,
                            uint32_t extra_reach_ahead) {
  const size_t total_bytes = sizeof(GreenInner) + children.size() * sizeof(GreenRef);
  void* mem = uast_alloc(total_bytes);
  auto* inner = new (mem) GreenInner();
  inner->rc.store(1, std::memory_order_relaxed);
  inner->kind = kind;
  inner->flags = flags & ~FLAG_LEAF;
  inner->child_count = static_cast<uint32_t>(children.size());
  inner->ctx_depth = ctx_depth;
  inner->ctx_hash = (ctx_depth == 0) ? 0u : ctx_hash;
  inner->prec = prec;
  inner->pred_deps_hash = pred_deps_hash;
  inner->seq_height = seq_height;

  auto* dst = reinterpret_cast<GreenRef*>(inner + 1);
  for (size_t i = 0; i < children.size(); ++i) {
    new (&dst[i]) GreenRef(children[i]);
    if (dst[i].node) {
      dst[i].node->retain();
    }
  }

  compute_inner_metrics(inner, children, extra_reach_ahead);
  return GreenPtr::adopt(inner);
}

} // namespace internal

// ============================================================================
// TokenInterner
// ============================================================================

namespace {

size_t next_power_of_two(size_t v) noexcept {
  size_t p = 1;
  while (p < v) p <<= 1;
  return p;
}

uint64_t hash_token_key(Kind kind, uint32_t width, uint32_t reach_ahead,
                        uint32_t flags, uint32_t text_hash,
                        uint32_t lex_state_in, uint32_t lex_state_out) noexcept {
  uint64_t h = 14695981039346656037ULL;
  auto mix = [&](uint64_t v) {
    h ^= v;
    h *= 1099511628211ULL;
  };
  mix(kind);
  mix(width);
  mix(reach_ahead);
  mix(flags);
  mix(text_hash);
  mix(lex_state_in);
  mix(lex_state_out);
  return h;
}

bool token_matches(const GreenToken* tok, Kind kind, uint32_t width,
                   uint32_t reach_ahead, uint32_t norm_flags, uint32_t text_hash,
                   uint32_t lex_state_in, uint32_t lex_state_out) noexcept {
  return tok->kind == kind &&
         tok->width == width &&
         tok->reach_ahead == reach_ahead &&
         tok->flags == norm_flags &&
         tok->text_hash == text_hash &&
         tok->lex_state_in == lex_state_in &&
         tok->lex_state_out == lex_state_out;
}

} // namespace

TokenInterner::TokenInterner(size_t max_capacity, uint32_t max_intern_width,
                             uint32_t max_intern_reach)
    : max_entries_(max_capacity),
      max_intern_width_(max_intern_width),
      max_intern_reach_(max_intern_reach) {
  if (max_capacity > 0) {
    // Keep load factor <= 0.70 by allocating >= max_capacity * 2 (power of 2)
    slot_count_ = next_power_of_two(std::max<size_t>(64, max_capacity * 2));
    mask_ = slot_count_ - 1;
    slots_ = new std::atomic<const GreenToken*>[slot_count_];
    for (size_t i = 0; i < slot_count_; ++i) {
      slots_[i].store(nullptr, std::memory_order_relaxed);
    }
  }
}

TokenInterner::~TokenInterner() {
  clear();
  delete[] slots_;
  slots_ = nullptr;
}

void TokenInterner::clear() {
  std::lock_guard<std::mutex> lock(write_mutex_);
  if (!slots_) return;
  for (size_t i = 0; i < slot_count_; ++i) {
    const GreenToken* tok = slots_[i].exchange(nullptr, std::memory_order_relaxed);
    if (tok) tok->release();
  }
  count_.store(0, std::memory_order_relaxed);
}

size_t TokenInterner::size() const noexcept {
  return count_.load(std::memory_order_relaxed);
}

GreenPtr TokenInterner::intern_or_create(Kind kind, uint32_t width, uint32_t reach_ahead,
                                         uint32_t flags, uint32_t text_hash,
                                         uint32_t lex_state_in, uint32_t lex_state_out) {
  uint32_t norm_flags = flags | FLAG_LEAF;
  if (norm_flags & (FLAG_ERROR | FLAG_MISSING)) {
    norm_flags |= FLAG_HAS_ERROR;
  }

  const bool eligible = slots_ != nullptr &&
                        width <= max_intern_width_ &&
                        reach_ahead <= max_intern_reach_ &&
                        count_.load(std::memory_order_relaxed) < max_entries_;

  if (!eligible) {
    return internal::allocate_token_raw(kind, width, reach_ahead, norm_flags,
                                        text_hash, lex_state_in, lex_state_out);
  }

  const uint64_t h = hash_token_key(kind, width, reach_ahead, norm_flags,
                                    text_hash, lex_state_in, lex_state_out);
  size_t idx = static_cast<size_t>(h) & mask_;

  // Lock-free probe
  for (size_t probe = 0; probe < slot_count_; ++probe) {
    const GreenToken* entry = slots_[idx].load(std::memory_order_acquire);
    if (!entry) break;
    if (token_matches(entry, kind, width, reach_ahead, norm_flags,
                      text_hash, lex_state_in, lex_state_out)) {
      return GreenPtr(entry); // retains
    }
    idx = (idx + 1) & mask_;
  }

  // Slow path: insert under mutex
  std::lock_guard<std::mutex> lock(write_mutex_);
  if (count_.load(std::memory_order_relaxed) >= max_entries_) {
    return internal::allocate_token_raw(kind, width, reach_ahead, norm_flags,
                                        text_hash, lex_state_in, lex_state_out);
  }

  idx = static_cast<size_t>(h) & mask_;
  for (size_t probe = 0; probe < slot_count_; ++probe) {
    const GreenToken* entry = slots_[idx].load(std::memory_order_relaxed);
    if (!entry) {
      GreenPtr created = internal::allocate_token_raw(kind, width, reach_ahead, norm_flags,
                                                      text_hash, lex_state_in, lex_state_out);
      const auto* raw = static_cast<const GreenToken*>(created.get());
      raw->retain(); // table holds 1 ref
      slots_[idx].store(raw, std::memory_order_release);
      count_.fetch_add(1, std::memory_order_relaxed);
      return created;
    }
    if (token_matches(entry, kind, width, reach_ahead, norm_flags,
                      text_hash, lex_state_in, lex_state_out)) {
      return GreenPtr(entry);
    }
    idx = (idx + 1) & mask_;
  }

  return internal::allocate_token_raw(kind, width, reach_ahead, norm_flags,
                                      text_hash, lex_state_in, lex_state_out);
}

// ============================================================================
// Direct Node Factories
// ============================================================================

GreenPtr make_token(Kind kind, uint32_t width, uint32_t reach_ahead,
                    uint32_t flags, uint32_t text_hash,
                    uint32_t lex_state_in, uint32_t lex_state_out) {
  return internal::allocate_token_raw(kind, width, reach_ahead, flags,
                                      text_hash, lex_state_in, lex_state_out);
}

GreenPtr make_inner(Kind kind, uint32_t flags, std::span<const GreenRef> children,
                    uint32_t ctx_depth, uint32_t ctx_hash,
                    uint32_t prec, uint32_t pred_deps_hash,
                    uint32_t extra_reach_ahead) {
  return internal::allocate_inner_raw(kind, flags & ~FLAG_SEQ, 0, children,
                                      ctx_depth, ctx_hash, prec,
                                      pred_deps_hash, extra_reach_ahead);
}

GreenPtr make_error(Kind kind, std::span<const GreenRef> children,
                    uint32_t extra_reach_ahead) {
  return internal::allocate_inner_raw(kind, FLAG_ERROR | FLAG_HAS_ERROR, 0, children,
                                      0, 0, 0, 0, extra_reach_ahead);
}

GreenPtr make_missing(Kind kind, uint32_t reach_ahead,
                      uint32_t lex_state_in, uint32_t lex_state_out) {
  return internal::allocate_token_raw(kind, 0, reach_ahead,
                                      FLAG_LEAF | FLAG_MISSING | FLAG_HAS_ERROR,
                                      0, lex_state_in, lex_state_out);
}

GreenPtr make_embed(Kind kind, std::span<const GreenRef> children,
                    uint32_t extra_reach_ahead) {
  return internal::allocate_inner_raw(kind, FLAG_EMBED, 0, children,
                                      0, 0, 0, 0, extra_reach_ahead);
}

// ============================================================================
// TreeBuilder
// ============================================================================

GreenPtr TreeBuilder::token(Kind kind, uint32_t width, uint32_t reach_ahead,
                            uint32_t flags, uint32_t text_hash,
                            uint32_t lex_state_in, uint32_t lex_state_out) {
  if (interner_) {
    return interner_->intern_or_create(kind, width, reach_ahead, flags,
                                       text_hash, lex_state_in, lex_state_out);
  }
  return make_token(kind, width, reach_ahead, flags, text_hash, lex_state_in, lex_state_out);
}

void TreeBuilder::begin_node(Kind kind, uint32_t flags, uint32_t prec) {
  Frame f;
  f.kind = kind;
  f.flags = flags & ~FLAG_LEAF & ~FLAG_SEQ;
  f.prec = prec;
  stack_.push_back(std::move(f));
}

void TreeBuilder::add_child(const GreenNode* node, FieldId field) {
  if (!node || stack_.empty()) return;
  node->retain();
  stack_.back().children.push_back(GreenRef{node, field});
}

void TreeBuilder::add_child(GreenPtr node, FieldId field) {
  if (!node || stack_.empty()) return;
  stack_.back().children.push_back(GreenRef{node.release_raw(), field});
}

namespace {

void append_seq_items(const GreenNode* node, std::vector<GreenRef>& out, FieldId default_field) {
  if (!node) return;
  if (!node->is_seq()) {
    node->retain();
    out.push_back(GreenRef{node, default_field});
    return;
  }
  const auto* seq = node->as_inner();
  if (seq->seq_height == 1) {
    for (const GreenRef& r : seq->children()) {
      if (r.node) r.node->retain();
      FieldId fid = (r.field_id != kNoField) ? r.field_id : default_field;
      out.push_back(GreenRef{r.node, fid});
    }
  } else {
    for (const GreenRef& r : seq->children()) {
      append_seq_items(r.node, out, default_field);
    }
  }
}

} // namespace

void TreeBuilder::add_seq(const GreenNode* seq, FieldId default_field) {
  if (!seq || stack_.empty()) return;
  if (seq->is_seq() && seq->as_inner()->logical_count <= kSeqThreshold) {
    append_seq_items(seq, stack_.back().children, default_field);
  } else {
    seq->retain();
    stack_.back().children.push_back(GreenRef{seq, default_field});
  }
}

void TreeBuilder::set_context(uint32_t ctx_depth, uint32_t ctx_hash) noexcept {
  if (stack_.empty()) return;
  stack_.back().ctx_depth = ctx_depth;
  stack_.back().ctx_hash = (ctx_depth == 0) ? 0u : ctx_hash;
}

void TreeBuilder::set_pred_deps(uint32_t pred_deps_hash) noexcept {
  if (stack_.empty()) return;
  stack_.back().pred_deps_hash = pred_deps_hash;
}

void TreeBuilder::add_extra_reach(uint32_t extra_reach_ahead) noexcept {
  if (stack_.empty()) return;
  if (extra_reach_ahead > stack_.back().extra_reach_ahead) {
    stack_.back().extra_reach_ahead = extra_reach_ahead;
  }
}

GreenPtr TreeBuilder::finalize_frame(Frame& f) {
  // Children in Frame already hold +1 ref each; transfer them cleanly via make_inner
  // and release Frame's own refs.
  GreenPtr node = make_inner(f.kind, f.flags, f.children,
                             f.ctx_depth, f.ctx_hash, f.prec,
                             f.pred_deps_hash, f.extra_reach_ahead);
  for (const GreenRef& r : f.children) {
    if (r.node) r.node->release();
  }
  f.children.clear();
  return node;
}

void TreeBuilder::end_node() {
  if (stack_.empty()) return;
  GreenPtr node = pop_node();
  if (!stack_.empty()) {
    stack_.back().children.push_back(GreenRef{node.release_raw(), kNoField});
  } else {
    root_ = std::move(node);
  }
}

GreenPtr TreeBuilder::pop_node() {
  if (stack_.empty()) return nullptr;
  Frame f = std::move(stack_.back());
  stack_.pop_back();
  return finalize_frame(f);
}

void TreeBuilder::abort_node() {
  if (stack_.empty()) return;
  for (const GreenRef& r : stack_.back().children) {
    if (r.node) r.node->release();
  }
  stack_.pop_back();
}

GreenPtr TreeBuilder::build() {
  while (!stack_.empty()) {
    end_node();
  }
  GreenPtr out = std::move(root_);
  root_.reset();
  return out;
}

uint32_t TreeBuilder::current_child_count() const noexcept {
  return stack_.empty() ? 0u : static_cast<uint32_t>(stack_.back().children.size());
}

// ============================================================================
// Token & Subtree Navigation Helpers (§6.2, §6.3, App B)
// ============================================================================

namespace {

std::optional<TokenHit> find_reaching_impl(const GreenNode* node, uint32_t offset,
                                           uint32_t tok_offset,
                                           uint32_t target_byte) noexcept {
  const uint64_t reach_end = static_cast<uint64_t>(offset) + node->width + node->reach_ahead;
  if (reach_end <= target_byte) {
    return std::nullopt;
  }

  if (node->is_token()) {
    return TokenHit{node->as_token(), offset, tok_offset};
  }

  const auto* inner = node->as_inner();
  uint32_t cur_off = offset;
  uint32_t cur_tok = tok_offset;
  for (const GreenRef& ref : inner->children()) {
    const GreenNode* child = ref.node;
    if (child) {
      if (auto hit = find_reaching_impl(child, cur_off, cur_tok, target_byte)) {
        return hit;
      }
      cur_off += child->width;
      cur_tok += child->get_token_count();
    }
  }
  return std::nullopt;
}

std::optional<TokenHit> find_at_offset_impl(const GreenNode* node, uint32_t offset,
                                            uint32_t tok_offset,
                                            uint32_t byte_offset) noexcept {
  if (node->is_missing()) {
    return std::nullopt;
  }

  if (node->is_token()) {
    const uint32_t end = offset + node->width;
    if ((node->width > 0 && byte_offset >= offset && byte_offset < end) ||
        (node->width == 0 && byte_offset == offset)) {
      return TokenHit{node->as_token(), offset, tok_offset};
    }
    return std::nullopt;
  }

  const uint32_t node_end = offset + node->width;
  if (byte_offset < offset || byte_offset > node_end) {
    return std::nullopt;
  }

  const auto* inner = node->as_inner();
  uint32_t cur_off = offset;
  uint32_t cur_tok = tok_offset;
  for (const GreenRef& ref : inner->children()) {
    const GreenNode* child = ref.node;
    if (!child) continue;
    const uint32_t child_end = cur_off + child->width;
    if (byte_offset <= child_end) {
      if (auto hit = find_at_offset_impl(child, cur_off, cur_tok, byte_offset)) {
        return hit;
      }
    }
    cur_off = child_end;
    cur_tok += child->get_token_count();
  }
  return std::nullopt;
}

} // namespace

std::optional<TokenHit> find_first_token_reaching(const GreenNode* root,
                                                  uint32_t target_byte) noexcept {
  if (!root) return std::nullopt;
  return find_reaching_impl(root, 0, 0, target_byte);
}

std::optional<TokenHit> find_token_at_offset(const GreenNode* root,
                                             uint32_t byte_offset) noexcept {
  if (!root) return std::nullopt;
  return find_at_offset_impl(root, 0, 0, byte_offset);
}

const GreenToken* first_token(const GreenNode* node) noexcept {
  if (!node) return nullptr;
  while (node && node->is_inner()) {
    const auto* inner = node->as_inner();
    const GreenNode* next = nullptr;
    for (const GreenRef& ref : inner->children()) {
      if (ref.node && ref.node->get_token_count() > 0) {
        next = ref.node;
        break;
      }
    }
    if (!next) return nullptr;
    node = next;
  }
  return node ? node->as_token() : nullptr;
}

const GreenToken* last_token(const GreenNode* node) noexcept {
  if (!node) return nullptr;
  while (node && node->is_inner()) {
    const auto* inner = node->as_inner();
    const auto children = inner->children();
    const GreenNode* next = nullptr;
    for (size_t i = children.size(); i > 0; --i) {
      const GreenRef& ref = children[i - 1];
      if (ref.node && ref.node->get_token_count() > 0) {
        next = ref.node;
        break;
      }
    }
    if (!next) return nullptr;
    node = next;
  }
  return node ? node->as_token() : nullptr;
}

// ============================================================================
// S-Expression Dump (§13.2)
// ============================================================================

namespace {

void append_escaped_text(std::string& out, std::string_view text) {
  out.push_back('"');
  for (char ch : text) {
    switch (ch) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out.push_back(ch); break;
    }
  }
  out.push_back('"');
}

void dump_node_sexp(const GreenNode* node, uint32_t offset, FieldId field_id,
                    const DumpOptions& opts, int indent, std::string& out,
                    bool& need_space) {
  if (!node) return;

  if (node->is_seq()) {
    const auto* seq = node->as_inner();
    uint32_t cur_off = offset;
    for (const GreenRef& ref : seq->children()) {
      if (ref.node) {
        FieldId fid = (ref.field_id != kNoField) ? ref.field_id : field_id;
        dump_node_sexp(ref.node, cur_off, fid, opts, indent, out, need_space);
        cur_off += ref.node->width;
      }
    }
    return;
  }

  if (node->is_token() && !opts.include_hidden && node->is_hidden()) {
    return;
  }

  if (need_space) {
    if (opts.pretty) {
      out.push_back('\n');
      out.append(static_cast<size_t>(indent) * 2, ' ');
    } else {
      out.push_back(' ');
    }
  }
  need_space = true;

  if (opts.include_fields && field_id != kNoField) {
    if (opts.field_name) {
      auto fn = opts.field_name(field_id, opts.user_ctx);
      if (!fn.empty()) {
        out.append(fn);
      } else {
        out += "field_" + std::to_string(field_id);
      }
    } else {
      out += "field_" + std::to_string(field_id);
    }
    out += ": ";
  }

  out.push_back('(');

  if (node->is_missing()) {
    out += "MISSING ";
  } else if (node->is_error() && !node->is_token()) {
    out += "ERROR";
  } else if (node->is_embed()) {
    out += "EMBED ";
  }

  if (!(node->is_error() && !node->is_token())) {
    if (opts.symbol_name) {
      auto sn = opts.symbol_name(node->kind, opts.user_ctx);
      if (!sn.empty()) {
        out.append(sn);
      } else {
        out += "sym_" + std::to_string(kind_symbol(node->kind));
      }
    } else {
      out += "sym_" + std::to_string(kind_symbol(node->kind));
    }
  }

  if (opts.include_ranges) {
    out += " [" + std::to_string(offset) + ".." + std::to_string(offset + node->width) + "]";
  }
  if (opts.include_reach) {
    out += " +" + std::to_string(node->reach_ahead);
  }

  if (node->is_token()) {
    std::string_view txt;
    if (opts.text_slice) {
      txt = opts.text_slice(offset, node->width, opts.user_ctx);
    } else if (!opts.source_text.empty() && offset + node->width <= opts.source_text.size()) {
      txt = opts.source_text.substr(offset, node->width);
    }
    if (!txt.empty()) {
      out.push_back(' ');
      append_escaped_text(out, txt);
    }
    out.push_back(')');
    return;
  }

  const auto* inner = node->as_inner();
  uint32_t cur_off = offset;
  bool child_space = true;
  for (const GreenRef& ref : inner->children()) {
    if (ref.node) {
      dump_node_sexp(ref.node, cur_off, ref.field_id, opts, indent + 1, out, child_space);
      cur_off += ref.node->width;
    }
  }
  out.push_back(')');
}

} // namespace

std::string dump_sexp(const GreenNode* root, const DumpOptions& opts) {
  if (!root) return {};
  std::string out;
  bool need_space = false;
  dump_node_sexp(root, 0, kNoField, opts, 0, out, need_space);
  return out;
}

} // namespace lx::uast
