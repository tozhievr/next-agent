#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lx::uast {

// ============================================================================
// Basic Identifiers & Primitives (§5.2, §5.3)
// ============================================================================

using Kind = uint32_t;
using FieldId = uint16_t;

constexpr FieldId kNoField = 0;
constexpr Kind kSeqKind = 0xFFFFFFFFu;

constexpr Kind make_kind(uint16_t lang_idx, uint16_t symbol_id) noexcept {
  return (static_cast<uint32_t>(lang_idx) << 16) | static_cast<uint32_t>(symbol_id);
}

constexpr uint16_t kind_lang(Kind k) noexcept {
  return static_cast<uint16_t>(k >> 16);
}

constexpr uint16_t kind_symbol(Kind k) noexcept {
  return static_cast<uint16_t>(k & 0xFFFFu);
}

struct Range {
  uint32_t start = 0;
  uint32_t end = 0;

  constexpr uint32_t length() const noexcept { return end > start ? end - start : 0; }
  constexpr bool empty() const noexcept { return start >= end; }
  constexpr bool contains(uint32_t byte_offset) const noexcept {
    return byte_offset >= start && byte_offset < end;
  }
  constexpr bool operator==(const Range&) const noexcept = default;
};

// Half-open dirty span [start, end) in old coordinates (matches ChangeSet Hunk).
struct DirtySpan {
  uint32_t start = 0;
  uint32_t end = 0;
  constexpr bool operator==(const DirtySpan&) const noexcept = default;
};

enum NodeFlags : uint32_t {
  FLAG_NONE      = 0,
  FLAG_LEAF      = 1u << 0,  // GreenToken
  FLAG_HIDDEN    = 1u << 1,  // Trivia (HIDDEN channel)
  FLAG_ERROR     = 1u << 2,  // Error recovery node or lex error token
  FLAG_MISSING   = 1u << 3,  // Zero-width Missing token/node
  FLAG_HAS_ERROR = 1u << 4,  // Subtree contains ERROR or MISSING
  FLAG_SEQ       = 1u << 5,  // Transparent Seq B-tree node (§5.4)
  FLAG_EMBED     = 1u << 6,  // Layer injection boundary marker (§9)
  FLAG_RULE      = 1u << 7,  // Grammar rule or labeled alternative
  FLAG_SUPERTYPE = 1u << 8,  // Rule marked SUPERTYPE (§5.2)
  FLAG_ALT_LABEL = 1u << 9,  // Labeled alternative (#Label)
  FLAG_TRUNCATED = 1u << 10, // Truncated by parser limits (§10.4)
};

// ============================================================================
// Allocator Hooks (optional host_api.alloc / free integration)
// ============================================================================

using AllocFn = void* (*)(size_t size);
using FreeFn = void (*)(void* ptr);

void set_allocator(AllocFn alloc_fn, FreeFn free_fn) noexcept;
void* uast_alloc(size_t size);
void uast_free(void* ptr) noexcept;

// ============================================================================
// Green Nodes (§5.3)
// ============================================================================

struct GreenToken;
struct GreenInner;

struct GreenHeader {
  mutable std::atomic<uint32_t> rc{1};
  uint32_t kind = 0;
  uint32_t width = 0;
  uint32_t reach_ahead = 0;
  uint32_t flags = 0;
  uint32_t child_count = 0; // 0 for Token; physical GreenRef count for Inner/Seq

  bool is_token() const noexcept { return (flags & FLAG_LEAF) != 0; }
  bool is_inner() const noexcept { return (flags & FLAG_LEAF) == 0; }
  bool is_seq() const noexcept { return (flags & FLAG_SEQ) != 0; }
  bool is_rule() const noexcept { return (flags & FLAG_RULE) != 0; }
  bool is_error() const noexcept { return (flags & FLAG_ERROR) != 0; }
  bool is_missing() const noexcept { return (flags & FLAG_MISSING) != 0; }
  bool is_hidden() const noexcept { return (flags & FLAG_HIDDEN) != 0; }
  bool has_error() const noexcept { return (flags & FLAG_HAS_ERROR) != 0; }
  bool is_embed() const noexcept { return (flags & FLAG_EMBED) != 0; }
  bool is_supertype() const noexcept { return (flags & FLAG_SUPERTYPE) != 0; }
  bool is_alt_label() const noexcept { return (flags & FLAG_ALT_LABEL) != 0; }
  bool is_truncated() const noexcept { return (flags & FLAG_TRUNCATED) != 0; }

  void retain() const noexcept { rc.fetch_add(1, std::memory_order_relaxed); }
  void release() const noexcept;
  uint32_t ref_count() const noexcept { return rc.load(std::memory_order_relaxed); }

  const GreenToken* as_token() const noexcept;
  const GreenInner* as_inner() const noexcept;
  uint32_t get_token_count() const noexcept;
};

using GreenNode = GreenHeader;

struct GreenRef {
  const GreenNode* node = nullptr;
  FieldId field_id = kNoField;

  bool operator==(const GreenRef&) const noexcept = default;
};

struct GreenToken : GreenHeader {
  uint32_t text_hash = 0;
  uint32_t lex_state_in = 0;
  uint32_t lex_state_out = 0;
  uint32_t _pad = 0;
};

static_assert(sizeof(GreenHeader) == 24, "GreenHeader expected to be 24 bytes");
static_assert(sizeof(GreenToken) == 40, "GreenToken expected to be 40 bytes");

struct GreenInner : GreenHeader {
  uint32_t token_count = 0;
  uint32_t ctx_depth = 0;
  uint32_t ctx_hash = 0;
  uint32_t prec = 0;
  uint32_t pred_deps_hash = 0;
  uint32_t struct_hash = 0;
  uint32_t logical_count = 0; // Total logical elements (flattening transparent Seq)
  uint32_t seq_height = 0;    // 0 for non-Seq; >= 1 for Seq B-tree nodes

  const GreenRef* children_ptr() const noexcept {
    return reinterpret_cast<const GreenRef*>(this + 1);
  }
  std::span<const GreenRef> children() const noexcept {
    return {children_ptr(), child_count};
  }
  const GreenRef& child(uint32_t idx) const noexcept {
    return children_ptr()[idx];
  }
};

static_assert(sizeof(GreenInner) == 56, "GreenInner expected to be 56 bytes (8-byte aligned)");
static_assert(alignof(GreenInner) >= alignof(GreenRef) || sizeof(GreenInner) % alignof(GreenRef) == 0,
              "Trailing GreenRef array must be properly aligned");

inline const GreenToken* GreenHeader::as_token() const noexcept {
  return is_token() ? static_cast<const GreenToken*>(this) : nullptr;
}

inline const GreenInner* GreenHeader::as_inner() const noexcept {
  return is_inner() ? static_cast<const GreenInner*>(this) : nullptr;
}

inline uint32_t GreenHeader::get_token_count() const noexcept {
  return is_token() ? 1u : static_cast<const GreenInner*>(this)->token_count;
}

// ============================================================================
// Intrusive Refcounted Handle: GreenPtr
// ============================================================================

class GreenPtr {
 public:
  GreenPtr() noexcept = default;
  GreenPtr(std::nullptr_t) noexcept {}

  explicit GreenPtr(const GreenNode* ptr) noexcept : ptr_(ptr) {
    if (ptr_) ptr_->retain();
  }

  static GreenPtr adopt(const GreenNode* ptr) noexcept {
    GreenPtr out;
    out.ptr_ = ptr;
    return out;
  }

  GreenPtr(const GreenPtr& other) noexcept : ptr_(other.ptr_) {
    if (ptr_) ptr_->retain();
  }

  GreenPtr(GreenPtr&& other) noexcept : ptr_(other.ptr_) {
    other.ptr_ = nullptr;
  }

  GreenPtr& operator=(const GreenPtr& other) noexcept {
    if (this != &other) {
      if (other.ptr_) other.ptr_->retain();
      if (ptr_) ptr_->release();
      ptr_ = other.ptr_;
    }
    return *this;
  }

  GreenPtr& operator=(GreenPtr&& other) noexcept {
    if (this != &other) {
      if (ptr_) ptr_->release();
      ptr_ = other.ptr_;
      other.ptr_ = nullptr;
    }
    return *this;
  }

  GreenPtr& operator=(std::nullptr_t) noexcept {
    reset();
    return *this;
  }

  ~GreenPtr() { reset(); }

  void reset() noexcept {
    if (ptr_) {
      ptr_->release();
      ptr_ = nullptr;
    }
  }

  const GreenNode* release_raw() noexcept {
    const GreenNode* p = ptr_;
    ptr_ = nullptr;
    return p;
  }

  const GreenNode* get() const noexcept { return ptr_; }
  const GreenNode* operator->() const noexcept { return ptr_; }
  const GreenNode& operator*() const noexcept { return *ptr_; }
  explicit operator bool() const noexcept { return ptr_ != nullptr; }

  const GreenToken* as_token() const noexcept { return ptr_ ? ptr_->as_token() : nullptr; }
  const GreenInner* as_inner() const noexcept { return ptr_ ? ptr_->as_inner() : nullptr; }

  bool operator==(const GreenPtr& other) const noexcept { return ptr_ == other.ptr_; }
  bool operator==(std::nullptr_t) const noexcept { return ptr_ == nullptr; }

 private:
  const GreenNode* ptr_ = nullptr;
};

// ============================================================================
// Reach Cleanliness (§5.5)
// ============================================================================

// X(N) = [node_start, node_start + node_width + reach_ahead).
// Clean iff NOT (dirty_start < X.end && dirty_end > X.start).
inline bool is_clean(uint32_t node_start, uint32_t node_width, uint32_t reach_ahead,
                     uint32_t dirty_start, uint32_t dirty_end) noexcept {
  const uint64_t x_start = node_start;
  const uint64_t x_end = static_cast<uint64_t>(node_start) + node_width + reach_ahead;
  return !(static_cast<uint64_t>(dirty_start) < x_end &&
           static_cast<uint64_t>(dirty_end) > x_start);
}

inline bool is_clean(uint32_t node_start, uint32_t node_width, uint32_t reach_ahead,
                     DirtySpan dirty) noexcept {
  return is_clean(node_start, node_width, reach_ahead, dirty.start, dirty.end);
}

// Dirty spans must be sorted by start.
bool is_clean(uint32_t node_start, uint32_t node_width, uint32_t reach_ahead,
              std::span<const DirtySpan> dirty) noexcept;

inline bool is_node_clean(const GreenNode* node, uint32_t node_start,
                          std::span<const DirtySpan> dirty) noexcept {
  return is_clean(node_start, node->width, node->reach_ahead, dirty);
}

// ============================================================================
// Hashing & Token Interner (§5.3, §10.1)
// ============================================================================

uint32_t fnv1a_32(const void* data, size_t len) noexcept;

inline uint32_t hash_text(std::string_view text) noexcept {
  return fnv1a_32(text.data(), text.size());
}

// Thread-safe token interner: lock-free reads, mutex-protected inserts.
// Deduplicates eligible tokens by (kind, width, reach_ahead, flags, text_hash,
// lex_state_in, lex_state_out).
class TokenInterner {
 public:
  explicit TokenInterner(size_t max_capacity = 65536,
                         uint32_t max_intern_width = 64,
                         uint32_t max_intern_reach = 8);
  ~TokenInterner();

  TokenInterner(const TokenInterner&) = delete;
  TokenInterner& operator=(const TokenInterner&) = delete;

  GreenPtr intern_or_create(Kind kind, uint32_t width, uint32_t reach_ahead,
                            uint32_t flags, uint32_t text_hash,
                            uint32_t lex_state_in = 0, uint32_t lex_state_out = 0);

  size_t size() const noexcept;
  size_t capacity() const noexcept { return slot_count_; }
  void clear();

 private:
  std::atomic<const GreenToken*>* slots_ = nullptr;
  size_t slot_count_ = 0;
  size_t mask_ = 0;
  size_t max_entries_ = 0;
  uint32_t max_intern_width_ = 64;
  uint32_t max_intern_reach_ = 8;
  std::atomic<size_t> count_{0};
  mutable std::mutex write_mutex_;
};

// ============================================================================
// Direct Node Factories
// ============================================================================

GreenPtr make_token(Kind kind, uint32_t width, uint32_t reach_ahead,
                    uint32_t flags, uint32_t text_hash,
                    uint32_t lex_state_in = 0, uint32_t lex_state_out = 0);

GreenPtr make_inner(Kind kind, uint32_t flags, std::span<const GreenRef> children,
                    uint32_t ctx_depth = 0, uint32_t ctx_hash = 0,
                    uint32_t prec = 0, uint32_t pred_deps_hash = 0,
                    uint32_t extra_reach_ahead = 0);

GreenPtr make_error(Kind kind, std::span<const GreenRef> children,
                    uint32_t extra_reach_ahead = 0);

GreenPtr make_missing(Kind kind, uint32_t reach_ahead = 0,
                      uint32_t lex_state_in = 0, uint32_t lex_state_out = 0);

GreenPtr make_embed(Kind kind, std::span<const GreenRef> children,
                    uint32_t extra_reach_ahead = 0);

// ============================================================================
// Transparent Seq B-Tree API (§5.4, §6.4)
// ============================================================================

constexpr uint32_t kSeqThreshold = 32;
constexpr uint32_t kSeqMinFanout = 8;
constexpr uint32_t kSeqMaxFanout = 16;

struct SeqSplitResult {
  GreenPtr left;
  GreenPtr right;
  bool exact_boundary = false;
  uint32_t split_index = 0;
  uint32_t split_byte = 0;
};

struct SeqItemHit {
  GreenRef ref;
  uint32_t logical_index = 0;
  uint32_t byte_offset = 0;
  uint32_t token_offset = 0;
};

// Builds a balanced Seq B-tree (fan-out 8..16) from logical items.
// Returns nullptr if items is empty.
GreenPtr seq_from_items(std::span<const GreenRef> items,
                        uint32_t ctx_depth = 0, uint32_t ctx_hash = 0,
                        uint32_t pred_deps_hash = 0,
                        uint32_t extra_reach_ahead = 0);

// Concatenates two Seq trees (either may be nullptr) in O(log n).
GreenPtr seq_concat(GreenPtr left, GreenPtr right);

// Splits seq at logical item index [0, count) in O(log n).
SeqSplitResult seq_split_at_index(const GreenNode* seq, uint32_t index);

// Splits seq at byte offset relative to seq start in O(log n).
// Items ending <= byte_offset go to left; remaining items go to right.
// exact_boundary is true iff left width == byte_offset (or byte_offset == 0 / seq->width).
SeqSplitResult seq_split_at_byte(const GreenNode* seq, uint32_t byte_offset);

// Metric lookups on Seq in O(log n).
std::optional<GreenRef> seq_get(const GreenNode* seq, uint32_t index);
std::optional<SeqItemHit> seq_find_by_byte(const GreenNode* seq, uint32_t byte_offset);
std::optional<SeqItemHit> seq_find_by_token(const GreenNode* seq, uint32_t token_index);

inline uint32_t seq_count(const GreenNode* seq) noexcept {
  if (!seq) return 0;
  if (seq->is_seq()) return seq->as_inner()->logical_count;
  return 1;
}

// Incremental loop splicing helpers (§6.4, App B.4), O(log n).
// seq_start_old is the byte offset of seq's start in old coordinates.
GreenPtr seq_longest_clean_prefix(const GreenNode* seq, uint32_t seq_start_old,
                                  std::span<const DirtySpan> dirty);

GreenPtr seq_clean_run_from(const GreenNode* seq, uint32_t seq_start_old,
                            uint32_t from_old_byte,
                            std::span<const DirtySpan> dirty);

// Updates root context / pred / extra reach metadata on a Seq root in O(1).
GreenPtr seq_with_context(const GreenNode* seq, uint32_t ctx_depth, uint32_t ctx_hash,
                          uint32_t pred_deps_hash, uint32_t extra_reach_ahead);

// U-Seq logical equality: compares logical trees ignoring physical Seq structure.
bool logical_equal(const GreenNode* a, const GreenNode* b) noexcept;

// ============================================================================
// TreeBuilder (§6.6)
// ============================================================================

class TreeBuilder {
 public:
  explicit TreeBuilder(TokenInterner* interner = nullptr) noexcept
      : interner_(interner) {}

  GreenPtr token(Kind kind, uint32_t width, uint32_t reach_ahead,
                 uint32_t flags, uint32_t text_hash,
                 uint32_t lex_state_in = 0, uint32_t lex_state_out = 0);

  void begin_node(Kind kind, uint32_t flags = FLAG_RULE, uint32_t prec = 0);
  void add_child(const GreenNode* node, FieldId field = kNoField);
  void add_child(GreenPtr node, FieldId field = kNoField);

  // Attaches a Seq node. If logical_count <= kSeqThreshold, unwraps elements into
  // current frame; if > kSeqThreshold, attaches Seq transparently.
  void add_seq(const GreenNode* seq, FieldId default_field = kNoField);

  void set_context(uint32_t ctx_depth, uint32_t ctx_hash) noexcept;
  void set_pred_deps(uint32_t pred_deps_hash) noexcept;
  void add_extra_reach(uint32_t extra_reach_ahead) noexcept;

  // Finalizes current frame and attaches it to parent (or sets root_ if top-level).
  void end_node();

  // Finalizes current frame and returns it WITHOUT attaching to parent.
  GreenPtr pop_node();

  // Discards current frame.
  void abort_node();

  // Returns finalized root and clears builder state.
  GreenPtr build();

  size_t depth() const noexcept { return stack_.size(); }
  uint32_t current_child_count() const noexcept;

 private:
  struct Frame {
    Kind kind = 0;
    uint32_t flags = FLAG_RULE;
    uint32_t prec = 0;
    uint32_t ctx_depth = 0;
    uint32_t ctx_hash = 0;
    uint32_t pred_deps_hash = 0;
    uint32_t extra_reach_ahead = 0;
    std::vector<GreenRef> children;
  };

  GreenPtr finalize_frame(Frame& f);

  TokenInterner* interner_ = nullptr;
  std::vector<Frame> stack_;
  GreenPtr root_;
};

// ============================================================================
// Token & Subtree Navigation Helpers (§6.2, §6.3, App B)
// ============================================================================

struct TokenHit {
  const GreenToken* token = nullptr;
  uint32_t offset = 0;
  uint32_t token_index = 0;
};

// Finds first token T in left-to-right order with (T.start + T.width + T.reach_ahead) > target_byte
// in O(log n) using reach_ahead pruning (§6.2 step 1).
std::optional<TokenHit> find_first_token_reaching(const GreenNode* root,
                                                  uint32_t target_byte) noexcept;

// Finds token covering byte_offset (or zero-width Missing token at byte_offset if no width>0 token).
std::optional<TokenHit> find_token_at_offset(const GreenNode* root,
                                             uint32_t byte_offset) noexcept;

const GreenToken* first_token(const GreenNode* node) noexcept;
const GreenToken* last_token(const GreenNode* node) noexcept;

// ============================================================================
// Red Layer: Cursor (§5.7)
// ============================================================================

class Cursor {
 public:
  struct Frame {
    const GreenNode* node = nullptr;
    uint32_t child_idx = 0;    // Physical child index in parent (0 for root)
    uint32_t offset = 0;       // Byte offset of this node
    uint32_t token_offset = 0; // Token index of this node
    FieldId field_id = kNoField;
  };

  Cursor() noexcept = default;
  explicit Cursor(GreenPtr root) noexcept;
  explicit Cursor(const GreenNode* root) noexcept;

  bool valid() const noexcept { return !path_.empty(); }
  void reset() noexcept;

  // Logical tree navigation (transparently skips Seq nodes).
  bool first_child() noexcept;
  bool last_child() noexcept;
  bool next_sibling() noexcept;
  bool prev_sibling() noexcept;
  bool parent() noexcept;

  // Leaf / token sequential traversal.
  bool next_leaf() noexcept;
  bool prev_leaf() noexcept;

  // O(log n) descent to deepest non-Seq node covering byte_offset (amortized O(1) sequentially).
  bool goto_byte(uint32_t byte_offset) noexcept;

  // O(log n) descent to token at token_index.
  bool goto_token(uint32_t token_index) noexcept;

  Kind kind() const noexcept;
  FieldId field() const noexcept;
  Range range() const noexcept;
  uint32_t width() const noexcept;
  uint32_t reach_ahead() const noexcept;
  uint32_t flags() const noexcept;
  uint32_t token_offset() const noexcept;
  uint32_t token_count() const noexcept;
  uint32_t prec() const noexcept;
  uint32_t ctx_depth() const noexcept;
  uint32_t ctx_hash() const noexcept;
  uint32_t pred_deps_hash() const noexcept;
  uint32_t struct_hash() const noexcept;

  bool is_token() const noexcept;
  const GreenNode* green_node() const noexcept;
  const GreenToken* as_token() const noexcept;
  const GreenInner* as_inner() const noexcept;
  const GreenNode* root_node() const noexcept { return root_.get(); }

  Cursor clone() const { return *this; }

 private:
  bool descend_first_non_seq() noexcept;
  bool descend_last_non_seq() noexcept;
  bool step_next_in_seq() noexcept;
  bool step_prev_in_seq() noexcept;
  void push_child(const GreenInner* parent_inner, uint32_t child_idx,
                  uint32_t child_byte_off, uint32_t child_tok_off) noexcept;

  GreenPtr root_;
  std::vector<Frame> path_;
};

// ============================================================================
// Structural Diff (§5.9, I5)
// ============================================================================

// Computes sorted, merged changed ranges in coordinates of new_root.
// Pure deletions at pos produce zero-width Range{pos, pos} (unless absorbed into adjacent change).
// Returns empty vector iff trees are structurally identical.
std::vector<Range> tree_diff(const GreenNode* old_root, const GreenNode* new_root);

// ============================================================================
// S-Expression Dump for Golden Tests (§13.2)
// ============================================================================

using SymbolNameFn = std::string_view (*)(Kind kind, void* ctx);
using FieldNameFn = std::string_view (*)(FieldId field, void* ctx);
using TextSliceFn = std::string_view (*)(uint32_t start, uint32_t len, void* ctx);

struct DumpOptions {
  SymbolNameFn symbol_name = nullptr;
  FieldNameFn field_name = nullptr;
  TextSliceFn text_slice = nullptr;
  void* user_ctx = nullptr;
  std::string_view source_text = {}; // Optional fallback if text_slice is null
  bool include_hidden = false;
  bool include_ranges = false;
  bool include_reach = false;
  bool include_fields = true;
  bool pretty = false;
};

std::string dump_sexp(const GreenNode* root, const DumpOptions& opts = {});

} // namespace lx::uast
