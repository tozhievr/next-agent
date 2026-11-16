#include "engine/rope/rope.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <new>

namespace lx {

namespace {

// ---------- Default Allocator ----------

void* LX_CALL default_alloc(size_t n) {
  return std::malloc(n);
}

void LX_CALL default_free(void* p) {
  std::free(p);
}

Rope::Allocator normalize_alloc(const lx_host_api* host) noexcept {
  if (host && host->alloc && host->free) {
    return {host->alloc, host->free};
  }
  return {&default_alloc, &default_free};
}

Rope::Allocator ensure_alloc(Rope::Allocator a) noexcept {
  if (a.alloc && a.free) {
    return a;
  }
  return {&default_alloc, &default_free};
}

// ---------- Rolling Monoidal Hash ----------

constexpr uint64_t kHashBase = 11400714819323198485ULL; // odd 64-bit multiplier

uint64_t pow_base(uint32_t exp) noexcept {
  uint64_t res = 1;
  uint64_t base = kHashBase;
  while (exp > 0) {
    if (exp & 1U) {
      res *= base;
    }
    base *= base;
    exp >>= 1U;
  }
  return res;
}

uint64_t poly_bytes(const uint8_t* data, uint32_t len) noexcept {
  uint64_t h = 0;
  for (uint32_t i = 0; i < len; ++i) {
    h = h * kHashBase + (static_cast<uint64_t>(data[i]) + 1ULL);
  }
  return h;
}

uint64_t mix64(uint64_t x) noexcept {
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  x = x ^ (x >> 31);
  return x;
}

// ---------- UTF-8 Helpers ----------

inline bool is_utf8_cont(uint8_t b) noexcept {
  return (b & 0xC0U) == 0x80U;
}

// Decodes one UTF-8 sequence starting at `p[0..rem)` and returns {byte_len, utf16_units}.
// Invalid/incomplete sequences consume 1 byte and count as 1 codepoint, 1 UTF-16 unit (U+FFFD).
struct Utf8Step {
  uint32_t byte_len;
  uint32_t utf16_len;
};

inline Utf8Step utf8_step(const uint8_t* p, uint32_t rem) noexcept {
  uint8_t b0 = p[0];
  if (b0 < 0x80U) {
    return {1, 1};
  }
  if ((b0 & 0xE0U) == 0xC0U) {
    if (rem >= 2 && b0 >= 0xC2U && is_utf8_cont(p[1])) {
      return {2, 1};
    }
    return {1, 1};
  }
  if ((b0 & 0xF0U) == 0xE0U) {
    if (rem >= 3 && is_utf8_cont(p[1]) && is_utf8_cont(p[2])) {
      uint32_t cp = ((uint32_t(b0 & 0x0FU) << 12) |
                     (uint32_t(p[1] & 0x3FU) << 6) |
                     uint32_t(p[2] & 0x3FU));
      if (cp >= 0x0800U && !(cp >= 0xD800U && cp <= 0xDFFFU)) {
        return {3, 1};
      }
    }
    return {1, 1};
  }
  if ((b0 & 0xF8U) == 0xF0U) {
    if (rem >= 4 && b0 <= 0xF4U &&
        is_utf8_cont(p[1]) && is_utf8_cont(p[2]) && is_utf8_cont(p[3])) {
      uint32_t cp = ((uint32_t(b0 & 0x07U) << 18) |
                     (uint32_t(p[1] & 0x3FU) << 12) |
                     (uint32_t(p[2] & 0x3FU) << 6) |
                     uint32_t(p[3] & 0x3FU));
      if (cp >= 0x10000U && cp <= 0x10FFFFU) {
        return {4, 2};
      }
    }
    return {1, 1};
  }
  return {1, 1};
}

inline bool is_valid_chunk_split(const uint8_t* data, uint32_t len, uint32_t pos) noexcept {
  if (pos == 0 || pos >= len) {
    return false;
  }
  if (is_utf8_cont(data[pos])) {
    return false;
  }
  if (data[pos - 1] == '\r' && data[pos] == '\n') {
    return false;
  }
  return true;
}

// Searches [lo, hi] for a valid split point closest to `target`. Returns UINT32_MAX if none.
uint32_t find_valid_split(const uint8_t* data, uint32_t len, uint32_t lo, uint32_t hi,
                          uint32_t target) noexcept {
  if (lo > hi) {
    return UINT32_MAX;
  }
  target = std::clamp(target, lo, hi);
  uint32_t max_d = std::max(target - lo, hi - target);
  for (uint32_t d = 0; d <= max_d; ++d) {
    if (target + d <= hi && is_valid_chunk_split(data, len, target + d)) {
      return target + d;
    }
    if (d > 0 && target >= lo + d && is_valid_chunk_split(data, len, target - d)) {
      return target - d;
    }
  }
  return UINT32_MAX;
}

// Same as find_valid_split, but falls back if input contains non-UTF-8 runs > 3 continuation bytes.
uint32_t find_split_or_fallback(const uint8_t* data, uint32_t len, uint32_t lo, uint32_t hi,
                                uint32_t target) noexcept {
  uint32_t p = find_valid_split(data, len, lo, hi, target);
  if (p != UINT32_MAX) {
    return p;
  }
  // Fallback for invalid UTF-8: at least avoid splitting \r\n if possible.
  target = std::clamp(target, lo, hi);
  for (uint32_t i = lo; i <= hi; ++i) {
    if (i > 0 && i < len && !(data[i - 1] == '\r' && data[i] == '\n')) {
      return i;
    }
  }
  return target;
}

} // namespace

// ---------- TextSummary ----------

TextSummary TextSummary::of(std::string_view utf8) noexcept {
  TextSummary s;
  const uint8_t* p = reinterpret_cast<const uint8_t*>(utf8.data());
  const uint32_t n = static_cast<uint32_t>(utf8.size());
  s.bytes = n;

  uint32_t i = 0;
  while (i < n) {
    if (p[i] < 0x80U) {
      if (p[i] == '\n') {
        if (s.newlines == 0) {
          s.first_line_bytes = i;
        }
        ++s.newlines;
        s.last_line_bytes = 0;
      } else {
        ++s.last_line_bytes;
      }
      ++s.chars;
      ++s.utf16;
      ++i;
    } else {
      Utf8Step step = utf8_step(p + i, n - i);
      ++s.chars;
      s.utf16 += step.utf16_len;
      s.last_line_bytes += step.byte_len;
      i += step.byte_len;
    }
  }
  if (s.newlines == 0) {
    s.first_line_bytes = n;
  }
  return s;
}

TextSummary TextSummary::operator+(const TextSummary& rhs) const noexcept {
  TextSummary r;
  r.bytes = bytes + rhs.bytes;
  r.chars = chars + rhs.chars;
  r.utf16 = utf16 + rhs.utf16;
  r.newlines = newlines + rhs.newlines;
  r.first_line_bytes = (newlines == 0) ? (bytes + rhs.first_line_bytes) : first_line_bytes;
  r.last_line_bytes = (rhs.newlines == 0) ? (last_line_bytes + rhs.bytes) : rhs.last_line_bytes;
  return r;
}

TextSummary& TextSummary::operator+=(const TextSummary& rhs) noexcept {
  *this = *this + rhs;
  return *this;
}

// ---------- ChangeSet ----------

void ChangeSet::normalize(std::vector<Hunk>& hunks) {
  std::erase_if(hunks, [](const Hunk& h) {
    return h.old_start == h.old_end && h.new_len == 0;
  });
  if (hunks.empty()) {
    return;
  }
  std::sort(hunks.begin(), hunks.end(), [](const Hunk& a, const Hunk& b) {
    if (a.old_start != b.old_start) {
      return a.old_start < b.old_start;
    }
    return a.old_end < b.old_end;
  });
  std::vector<Hunk> out;
  out.reserve(hunks.size());
  for (const Hunk& h : hunks) {
    assert(h.old_start <= h.old_end);
    if (!out.empty() && h.old_start <= out.back().old_end) {
      // Coalesce touching or overlapping hunks.
      out.back().old_end = std::max(out.back().old_end, h.old_end);
      out.back().new_len += h.new_len;
    } else {
      out.push_back(h);
    }
  }
  hunks = std::move(out);
}

ChangeSet ChangeSet::single(uint32_t old_start, uint32_t old_end, uint32_t new_len) {
  if (old_start > old_end) {
    std::swap(old_start, old_end);
  }
  if (old_start == old_end && new_len == 0) {
    return ChangeSet{};
  }
  return ChangeSet(std::vector<Hunk>{Hunk{old_start, old_end, new_len}});
}

ChangeSet ChangeSet::from_hunks(std::span<const Hunk> hunks) {
  std::vector<Hunk> v(hunks.begin(), hunks.end());
  for (auto& h : v) {
    if (h.old_start > h.old_end) {
      std::swap(h.old_start, h.old_end);
    }
  }
  normalize(v);
  return ChangeSet(std::move(v));
}

uint32_t ChangeSet::map_pos(uint32_t old_pos, Assoc assoc) const noexcept {
  int64_t delta = 0;
  for (const Hunk& h : hunks_) {
    if (old_pos < h.old_start) {
      return static_cast<uint32_t>(static_cast<int64_t>(old_pos) + delta);
    }
    int64_t new_start = static_cast<int64_t>(h.old_start) + delta;
    int64_t new_end = new_start + static_cast<int64_t>(h.new_len);
    if (h.old_start == h.old_end) {
      // Pure insertion at h.old_start.
      if (old_pos == h.old_start) {
        return static_cast<uint32_t>(assoc == Assoc::Before ? new_start : new_end);
      }
    } else {
      // Replacement or deletion [old_start, old_end).
      if (old_pos <= h.old_end) {
        if (old_pos == h.old_end && assoc == Assoc::After) {
          return static_cast<uint32_t>(new_end);
        }
        if (old_pos == h.old_start && assoc == Assoc::Before) {
          return static_cast<uint32_t>(new_start);
        }
        return static_cast<uint32_t>(assoc == Assoc::Before ? new_start : new_end);
      }
    }
    delta += static_cast<int64_t>(h.new_len) - static_cast<int64_t>(h.old_end - h.old_start);
  }
  return static_cast<uint32_t>(static_cast<int64_t>(old_pos) + delta);
}

std::optional<uint32_t> ChangeSet::unmap_pos(uint32_t new_pos, Assoc assoc) const noexcept {
  int64_t delta = 0;
  for (const Hunk& h : hunks_) {
    int64_t new_start = static_cast<int64_t>(h.old_start) + delta;
    int64_t new_end = new_start + static_cast<int64_t>(h.new_len);
    int64_t p = static_cast<int64_t>(new_pos);

    if (p < new_start) {
      return static_cast<uint32_t>(p - delta);
    }
    if (h.new_len == 0) {
      // Pure deletion: new_start == new_end.
      if (p == new_start) {
        return (assoc == Assoc::Before) ? h.old_start : h.old_end;
      }
    } else {
      if (p == new_start) {
        if (assoc == Assoc::Before) {
          return h.old_start;
        }
        return std::nullopt;
      }
      if (p < new_end) {
        return std::nullopt;
      }
      if (p == new_end) {
        if (assoc == Assoc::After) {
          return h.old_end;
        }
        return std::nullopt;
      }
    }
    delta += static_cast<int64_t>(h.new_len) - static_cast<int64_t>(h.old_end - h.old_start);
  }
  return static_cast<uint32_t>(static_cast<int64_t>(new_pos) - delta);
}

ChangeSet ChangeSet::compose(const ChangeSet& next) const {
  if (hunks_.empty()) {
    return next;
  }
  if (next.hunks_.empty()) {
    return *this;
  }

  const auto& A = hunks_;
  const auto& B = next.hunks_;
  std::vector<Hunk> out;
  out.reserve(A.size() + B.size());

  size_t i = 0;
  size_t j = 0;
  int64_t delta1 = 0; // accumulated V1 - V0 across A[0..i)

  auto a_v1_start = [&](size_t idx, int64_t d) -> int64_t {
    return static_cast<int64_t>(A[idx].old_start) + d;
  };
  auto a_v1_end = [&](size_t idx, int64_t d) -> int64_t {
    return static_cast<int64_t>(A[idx].old_start) + d + static_cast<int64_t>(A[idx].new_len);
  };
  auto a_delta = [&](size_t idx) -> int64_t {
    return static_cast<int64_t>(A[idx].new_len) -
           static_cast<int64_t>(A[idx].old_end - A[idx].old_start);
  };

  while (i < A.size() && j < B.size()) {
    int64_t a_s = a_v1_start(i, delta1);
    int64_t a_e = a_v1_end(i, delta1);
    int64_t b_s = static_cast<int64_t>(B[j].old_start);
    int64_t b_e = static_cast<int64_t>(B[j].old_end);

    if (a_e < b_s) {
      out.push_back(A[i]);
      delta1 += a_delta(i);
      ++i;
      continue;
    }
    if (b_e < a_s) {
      out.push_back(Hunk{
          static_cast<uint32_t>(b_s - delta1),
          static_cast<uint32_t>(b_e - delta1),
          B[j].new_len,
      });
      ++j;
      continue;
    }

    // Overlapping or touching cluster in V1.
    int64_t v1_s = std::min(a_s, b_s);
    int64_t v1_e = std::max(a_e, b_e);

    int64_t v0_s = 0;
    if (a_s <= b_s) {
      v0_s = static_cast<int64_t>(A[i].old_start);
    } else {
      v0_s = b_s - delta1;
    }

    int64_t sum_b_old = 0;
    int64_t sum_b_new = 0;

    // Seed with whichever starts first (or both if overlapping).
    if (a_s <= v1_e) {
      delta1 += a_delta(i);
      ++i;
    }
    if (b_s <= v1_e) {
      sum_b_old += (b_e - b_s);
      sum_b_new += static_cast<int64_t>(B[j].new_len);
      ++j;
    }

    bool advanced = true;
    int64_t last_a_v1_e = (a_s <= v1_e) ? a_e : -1;
    uint32_t last_a_v0_e = (a_s <= v1_e) ? A[i - 1].old_end : 0;

    while (advanced) {
      advanced = false;
      while (i < A.size()) {
        int64_t ns = a_v1_start(i, delta1);
        if (ns <= v1_e) {
          int64_t ne = a_v1_end(i, delta1);
          if (ne >= v1_e) {
            v1_e = ne;
            last_a_v1_e = ne;
            last_a_v0_e = A[i].old_end;
          }
          delta1 += a_delta(i);
          ++i;
          advanced = true;
        } else {
          break;
        }
      }
      while (j < B.size()) {
        int64_t ns = static_cast<int64_t>(B[j].old_start);
        if (ns <= v1_e) {
          int64_t ne = static_cast<int64_t>(B[j].old_end);
          if (ne > v1_e) {
            v1_e = ne;
          }
          sum_b_old += (ne - ns);
          sum_b_new += static_cast<int64_t>(B[j].new_len);
          ++j;
          advanced = true;
        } else {
          break;
        }
      }
    }

    int64_t v0_e = 0;
    if (last_a_v1_e == v1_e) {
      v0_e = static_cast<int64_t>(last_a_v0_e);
    } else {
      v0_e = v1_e - delta1;
    }

    int64_t v2_len = (v1_e - v1_s) - sum_b_old + sum_b_new;
    assert(v0_e >= v0_s);
    assert(v2_len >= 0);

    out.push_back(Hunk{
        static_cast<uint32_t>(v0_s),
        static_cast<uint32_t>(v0_e),
        static_cast<uint32_t>(v2_len),
    });
  }

  while (i < A.size()) {
    out.push_back(A[i]);
    delta1 += a_delta(i);
    ++i;
  }
  while (j < B.size()) {
    out.push_back(Hunk{
        static_cast<uint32_t>(static_cast<int64_t>(B[j].old_start) - delta1),
        static_cast<uint32_t>(static_cast<int64_t>(B[j].old_end) - delta1),
        B[j].new_len,
    });
    ++j;
  }

  normalize(out);
  return ChangeSet(std::move(out));
}

bool ChangeSet::is_clean(uint32_t x_start, uint32_t x_end) const noexcept {
  if (hunks_.empty()) {
    return true;
  }
  // Binary search for first hunk with old_end >= x_start (or old_start >= x_start for zero-len).
  auto it = std::lower_bound(hunks_.begin(), hunks_.end(), x_start,
                             [](const Hunk& h, uint32_t s) { return h.old_end < s; });
  // Check if any hunk starting from (it > begin ? it-1 : it) intersects [x_start, x_end).
  // Formula from §5.5: dirty iff d.old_start < x_end && d.old_end > x_start.
  if (it != hunks_.begin()) {
    auto prev = it - 1;
    if (prev->old_start < x_end && prev->old_end > x_start) {
      return false;
    }
  }
  for (; it != hunks_.end(); ++it) {
    if (it->old_start >= x_end) {
      break;
    }
    if (it->old_start < x_end && it->old_end > x_start) {
      return false;
    }
  }
  return true;
}

int64_t ChangeSet::net_delta() const noexcept {
  int64_t d = 0;
  for (const Hunk& h : hunks_) {
    d += static_cast<int64_t>(h.new_len) - static_cast<int64_t>(h.old_end - h.old_start);
  }
  return d;
}

uint32_t ChangeSet::new_length(uint32_t old_length) const noexcept {
  int64_t res = static_cast<int64_t>(old_length) + net_delta();
  return res > 0 ? static_cast<uint32_t>(res) : 0U;
}

uint32_t ChangeSet::old_changed_bytes() const noexcept {
  uint64_t s = 0;
  for (const Hunk& h : hunks_) {
    s += (h.old_end - h.old_start);
  }
  return static_cast<uint32_t>(std::min<uint64_t>(s, UINT32_MAX));
}

uint32_t ChangeSet::new_changed_bytes() const noexcept {
  uint64_t s = 0;
  for (const Hunk& h : hunks_) {
    s += h.new_len;
  }
  return static_cast<uint32_t>(std::min<uint64_t>(s, UINT32_MAX));
}

double ChangeSet::change_ratio(uint32_t old_length) const noexcept {
  if (hunks_.empty()) {
    return 0.0;
  }
  uint64_t changed = 0;
  for (const Hunk& h : hunks_) {
    changed += std::max(h.old_end - h.old_start, h.new_len);
  }
  uint32_t denom = std::max<uint32_t>(old_length, 1U);
  return static_cast<double>(changed) / static_cast<double>(denom);
}

// ---------- Rope::Node ----------

struct Rope::Node {
  std::atomic<uint32_t> rc{1};
  uint8_t height = 0; // 0 = leaf, >= 1 = internal
  uint8_t count = 0;  // internal child count (0 for leaf)
  TextSummary summary{};
  uint64_t poly = 0;
  void (LX_CALL *free_fn)(void*) = nullptr;
};

namespace {

inline const uint8_t* leaf_data(const Rope::Node* n) noexcept {
  return reinterpret_cast<const uint8_t*>(n + 1);
}

inline uint8_t* leaf_data_mut(Rope::Node* n) noexcept {
  return reinterpret_cast<uint8_t*>(n + 1);
}

inline const Rope::Node* const* internal_children(const Rope::Node* n) noexcept {
  return reinterpret_cast<const Rope::Node* const*>(n + 1);
}

inline const Rope::Node** internal_children_mut(Rope::Node* n) noexcept {
  return reinterpret_cast<const Rope::Node**>(n + 1);
}

inline void retain_node(const Rope::Node* n) noexcept {
  if (n) {
    const_cast<Rope::Node*>(n)->rc.fetch_add(1, std::memory_order_relaxed);
  }
}

void release_node(const Rope::Node* n) noexcept {
  if (!n) {
    return;
  }
  auto* mut = const_cast<Rope::Node*>(n);
  if (mut->rc.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    if (mut->height > 0) {
      const Rope::Node* const* ch = internal_children(mut);
      for (uint8_t i = 0; i < mut->count; ++i) {
        release_node(ch[i]);
      }
    }
    auto free_fn = mut->free_fn ? mut->free_fn : &default_free;
    mut->~Node();
    free_fn(mut);
  }
}

const Rope::Node* make_leaf(const uint8_t* data, uint32_t len, Rope::Allocator alloc) {
  alloc = ensure_alloc(alloc);
  void* mem = alloc.alloc(sizeof(Rope::Node) + len);
  if (!mem) {
    throw std::bad_alloc();
  }
  auto* node = new (mem) Rope::Node();
  node->height = 0;
  node->count = 0;
  node->free_fn = alloc.free;
  if (len > 0) {
    std::memcpy(leaf_data_mut(node), data, len);
  }
  node->summary = TextSummary::of(std::string_view(reinterpret_cast<const char*>(data), len));
  node->poly = poly_bytes(data, len);
  return node;
}

// Takes ownership of ONE refcount per pointer in `children[0..count)`!
const Rope::Node* make_internal(const Rope::Node* const* children, uint8_t count,
                                Rope::Allocator alloc) {
  assert(count >= 1 && count <= Rope::kMaxInternalChildren);
  alloc = ensure_alloc(alloc);
  void* mem = alloc.alloc(sizeof(Rope::Node) + sizeof(const Rope::Node*) * count);
  if (!mem) {
    for (uint8_t i = 0; i < count; ++i) {
      release_node(children[i]);
    }
    throw std::bad_alloc();
  }
  auto* node = new (mem) Rope::Node();
  node->height = static_cast<uint8_t>(children[0]->height + 1);
  node->count = count;
  node->free_fn = alloc.free;

  const Rope::Node** dst = internal_children_mut(node);
  TextSummary sum{};
  uint64_t poly = 0;
  for (uint8_t i = 0; i < count; ++i) {
    dst[i] = children[i];
    sum += children[i]->summary;
    poly = poly * pow_base(children[i]->summary.bytes) + children[i]->poly;
  }
  node->summary = sum;
  node->poly = poly;
  return node;
}

// Builds a balanced B+-tree from an array of nodes of uniform height `h`.
// Consumes 1 refcount per node in `nodes`.
const Rope::Node* build_from_uniform(std::vector<const Rope::Node*>& nodes,
                                     Rope::Allocator alloc) {
  if (nodes.empty()) {
    return nullptr;
  }
  while (nodes.size() > 1) {
    std::vector<const Rope::Node*> next;
    const size_t n = nodes.size();
    next.reserve((n + Rope::kMaxInternalChildren - 1) / Rope::kMaxInternalChildren);

    size_t i = 0;
    while (i < n) {
      size_t rem = n - i;
      size_t take = 0;
      if (rem <= Rope::kMaxInternalChildren) {
        take = rem;
      } else if (rem <= Rope::kMaxInternalChildren + Rope::kMinInternalChildren) {
        // Split evenly so both this group and the last group have >= kMinInternalChildren (8).
        take = rem / 2;
      } else {
        take = 12; // midpoint fan-out in [8, 16]
      }
      next.push_back(make_internal(nodes.data() + i, static_cast<uint8_t>(take), alloc));
      i += take;
    }
    nodes = std::move(next);
  }
  return nodes[0];
}

// Chunks `data[0..len)` into valid leaves satisfying R2 and R4.
// Only if `allow_oversize_sole_leaf` is true (whole doc is 1025..1027 with 4-byte UTF-8 straddling
// 512..514) may a single leaf of 1025..1027 bytes be emitted.
void chunk_bytes_into_leaves(const uint8_t* data, uint32_t len, bool allow_sole_leaf,
                             Rope::Allocator alloc, std::vector<const Rope::Node*>& out) {
  if (len == 0) {
    return;
  }
  uint32_t pos = 0;
  while (pos < len) {
    uint32_t rem = len - pos;
    if (rem <= Rope::kMaxLeafBytes) {
      out.push_back(make_leaf(data + pos, rem, alloc));
      break;
    }
    if (rem >= 1025 && rem <= 1027) {
      uint32_t split = find_valid_split(data + pos, rem, Rope::kMinLeafBytes,
                                        rem - Rope::kMinLeafBytes, rem / 2);
      if (split != UINT32_MAX) {
        out.push_back(make_leaf(data + pos, split, alloc));
        out.push_back(make_leaf(data + pos + split, rem - split, alloc));
        break;
      }
      if (allow_sole_leaf && out.empty()) {
        out.push_back(make_leaf(data + pos, rem, alloc));
        break;
      }
      // Fallback (should not happen for valid UTF-8 when neighbors were merged).
      split = find_split_or_fallback(data + pos, rem, Rope::kMinLeafBytes,
                                     rem - Rope::kMinLeafBytes, rem / 2);
      out.push_back(make_leaf(data + pos, split, alloc));
      out.push_back(make_leaf(data + pos + split, rem - split, alloc));
      break;
    }
    if (rem <= 1536) {
      uint32_t split = find_split_or_fallback(data + pos, rem, Rope::kMinLeafBytes,
                                              rem - Rope::kMinLeafBytes, rem / 2);
      out.push_back(make_leaf(data + pos, split, alloc));
      out.push_back(make_leaf(data + pos + split, rem - split, alloc));
      break;
    }
    if (rem <= 2044) {
      uint32_t split = find_split_or_fallback(data + pos, rem, rem - Rope::kMaxLeafBytes,
                                              Rope::kMaxLeafBytes, rem / 2);
      out.push_back(make_leaf(data + pos, split, alloc));
      out.push_back(make_leaf(data + pos + split, rem - split, alloc));
      break;
    }
    // rem >= 2045: pick ~768 bytes, leaving >= 1273 >= 1028.
    uint32_t split = find_split_or_fallback(data + pos, rem, 760, 776, 768);
    out.push_back(make_leaf(data + pos, split, alloc));
    pos += split;
  }
}

// Helper for B+-tree concat: returns 1 or 2 nodes of height `a->height`.
// Consumes 1 ref on `a` and 1 ref on `b`.
struct Pair {
  const Rope::Node* first = nullptr;
  const Rope::Node* second = nullptr;
};

Pair merge_or_split_nodes(const Rope::Node* const* items, size_t total,
                          Rope::Allocator alloc) {
  if (total <= Rope::kMaxInternalChildren) {
    return {make_internal(items, static_cast<uint8_t>(total), alloc), nullptr};
  }
  size_t left_n = total / 2;
  size_t right_n = total - left_n;
  return {
      make_internal(items, static_cast<uint8_t>(left_n), alloc),
      make_internal(items + left_n, static_cast<uint8_t>(right_n), alloc),
  };
}

// `a->height == b->height > 0`, returns 1 or 2 nodes of height `a->height`.
// Consumes 1 ref on `a` and `b`.
Pair merge_same_height(const Rope::Node* a, const Rope::Node* b, Rope::Allocator alloc) {
  assert(a->height == b->height && a->height > 0);
  const Rope::Node* buf[Rope::kMaxInternalChildren * 2];
  size_t n = 0;
  const Rope::Node* const* ach = internal_children(a);
  const Rope::Node* const* bch = internal_children(b);
  for (uint8_t i = 0; i < a->count; ++i) {
    retain_node(ach[i]);
    buf[n++] = ach[i];
  }
  for (uint8_t i = 0; i < b->count; ++i) {
    retain_node(bch[i]);
    buf[n++] = bch[i];
  }
  release_node(a);
  release_node(b);
  return merge_or_split_nodes(buf, n, alloc);
}

// `a->height > b->height`. Consumes 1 ref on `a` and `b`.
Pair insert_right(const Rope::Node* a, const Rope::Node* b, Rope::Allocator alloc) {
  if (a->height == b->height + 1) {
    const Rope::Node* buf[Rope::kMaxInternalChildren + 1];
    size_t n = 0;
    const Rope::Node* const* ach = internal_children(a);
    if (b->height > 0 && b->count < Rope::kMinInternalChildren) {
      for (uint8_t i = 0; i + 1 < a->count; ++i) {
        retain_node(ach[i]);
        buf[n++] = ach[i];
      }
      retain_node(ach[a->count - 1]);
      Pair merged = merge_same_height(ach[a->count - 1], b, alloc);
      buf[n++] = merged.first;
      if (merged.second) {
        buf[n++] = merged.second;
      }
    } else {
      for (uint8_t i = 0; i < a->count; ++i) {
        retain_node(ach[i]);
        buf[n++] = ach[i];
      }
      buf[n++] = b;
    }
    release_node(a);
    return merge_or_split_nodes(buf, n, alloc);
  }

  const Rope::Node* const* ach = internal_children(a);
  retain_node(ach[a->count - 1]);
  Pair sub = insert_right(ach[a->count - 1], b, alloc);

  const Rope::Node* buf[Rope::kMaxInternalChildren + 1];
  size_t n = 0;
  for (uint8_t i = 0; i + 1 < a->count; ++i) {
    retain_node(ach[i]);
    buf[n++] = ach[i];
  }
  buf[n++] = sub.first;
  if (sub.second) {
    buf[n++] = sub.second;
  }
  release_node(a);
  return merge_or_split_nodes(buf, n, alloc);
}

// `a->height < b->height`. Consumes 1 ref on `a` and `b`.
Pair insert_left(const Rope::Node* a, const Rope::Node* b, Rope::Allocator alloc) {
  if (b->height == a->height + 1) {
    const Rope::Node* buf[Rope::kMaxInternalChildren + 1];
    size_t n = 0;
    const Rope::Node* const* bch = internal_children(b);
    if (a->height > 0 && a->count < Rope::kMinInternalChildren) {
      retain_node(bch[0]);
      Pair merged = merge_same_height(a, bch[0], alloc);
      buf[n++] = merged.first;
      if (merged.second) {
        buf[n++] = merged.second;
      }
      for (uint8_t i = 1; i < b->count; ++i) {
        retain_node(bch[i]);
        buf[n++] = bch[i];
      }
    } else {
      buf[n++] = a;
      for (uint8_t i = 0; i < b->count; ++i) {
        retain_node(bch[i]);
        buf[n++] = bch[i];
      }
    }
    release_node(b);
    return merge_or_split_nodes(buf, n, alloc);
  }

  const Rope::Node* const* bch = internal_children(b);
  retain_node(bch[0]);
  Pair sub = insert_left(a, bch[0], alloc);

  const Rope::Node* buf[Rope::kMaxInternalChildren + 1];
  size_t n = 0;
  buf[n++] = sub.first;
  if (sub.second) {
    buf[n++] = sub.second;
  }
  for (uint8_t i = 1; i < b->count; ++i) {
    retain_node(bch[i]);
    buf[n++] = bch[i];
  }
  release_node(b);
  return merge_or_split_nodes(buf, n, alloc);
}

// Concatenates two valid B+-trees. Consumes 1 ref on `a` and `b`.
const Rope::Node* concat_trees(const Rope::Node* a, const Rope::Node* b,
                               Rope::Allocator alloc) {
  if (!a) {
    return b;
  }
  if (!b) {
    return a;
  }
  if (a->height == b->height) {
    if (a->height == 0) {
      const Rope::Node* pair[2] = {a, b};
      return make_internal(pair, 2, alloc);
    }
    if (a->count + b->count <= Rope::kMaxInternalChildren) {
      const Rope::Node* buf[Rope::kMaxInternalChildren];
      size_t n = 0;
      const Rope::Node* const* ach = internal_children(a);
      const Rope::Node* const* bch = internal_children(b);
      for (uint8_t i = 0; i < a->count; ++i) {
        retain_node(ach[i]);
        buf[n++] = ach[i];
      }
      for (uint8_t i = 0; i < b->count; ++i) {
        retain_node(bch[i]);
        buf[n++] = bch[i];
      }
      release_node(a);
      release_node(b);
      return make_internal(buf, static_cast<uint8_t>(n), alloc);
    }
    if (a->count >= Rope::kMinInternalChildren && b->count >= Rope::kMinInternalChildren) {
      const Rope::Node* pair[2] = {a, b};
      return make_internal(pair, 2, alloc);
    }
    // One root had < 8 children and total > 16: redistribute evenly.
    const Rope::Node* buf[Rope::kMaxInternalChildren * 2];
    size_t n = 0;
    const Rope::Node* const* ach = internal_children(a);
    const Rope::Node* const* bch = internal_children(b);
    for (uint8_t i = 0; i < a->count; ++i) {
      retain_node(ach[i]);
      buf[n++] = ach[i];
    }
    for (uint8_t i = 0; i < b->count; ++i) {
      retain_node(bch[i]);
      buf[n++] = bch[i];
    }
    release_node(a);
    release_node(b);
    Pair p = merge_or_split_nodes(buf, n, alloc);
    const Rope::Node* pair[2] = {p.first, p.second};
    return make_internal(pair, 2, alloc);
  }
  if (a->height > b->height) {
    Pair p = insert_right(a, b, alloc);
    if (!p.second) {
      return p.first;
    }
    const Rope::Node* pair[2] = {p.first, p.second};
    return make_internal(pair, 2, alloc);
  }
  Pair p = insert_left(a, b, alloc);
  if (!p.second) {
    return p.first;
  }
  const Rope::Node* pair[2] = {p.first, p.second};
  return make_internal(pair, 2, alloc);
}

struct SplitResult {
  const Rope::Node* tree = nullptr; // caller owns 1 ref
  const Rope::Node* leaf = nullptr; // caller owns 1 ref
  uint32_t leaf_start = 0;
};

// Given `pos` in [1, root->summary.bytes], finds the leaf containing byte `pos - 1`
// and builds a valid B+-tree of all leaves strictly to its left. Does NOT consume `root`'s ref.
SplitResult split_left_of(const Rope::Node* root, uint32_t pos, Rope::Allocator alloc) {
  assert(root && pos >= 1 && pos <= root->summary.bytes);
  const Rope::Node* left_tree = nullptr;
  const Rope::Node* cur = root;
  uint32_t rem = pos;
  uint32_t leaf_start = 0;

  while (cur->height > 0) {
    const Rope::Node* const* ch = internal_children(cur);
    uint8_t idx = 0;
    while (idx + 1 < cur->count && rem > ch[idx]->summary.bytes) {
      rem -= ch[idx]->summary.bytes;
      leaf_start += ch[idx]->summary.bytes;
      ++idx;
    }
    if (idx > 0) {
      const Rope::Node* slice = nullptr;
      if (idx == 1) {
        retain_node(ch[0]);
        slice = ch[0];
      } else {
        for (uint8_t k = 0; k < idx; ++k) {
          retain_node(ch[k]);
        }
        slice = make_internal(ch, idx, alloc);
      }
      left_tree = concat_trees(left_tree, slice, alloc);
    }
    cur = ch[idx];
  }
  retain_node(cur);
  return {left_tree, cur, leaf_start};
}

// Given `pos` in [0, root->summary.bytes - 1], finds the leaf containing byte `pos`
// and builds a valid B+-tree of all leaves strictly to its right. Does NOT consume `root`'s ref.
SplitResult split_right_of(const Rope::Node* root, uint32_t pos, Rope::Allocator alloc) {
  assert(root && pos < root->summary.bytes);
  // Collect right slices top-down, then concat bottom-up (or right-to-left).
  struct SliceRef {
    const Rope::Node* node;
  };
  const Rope::Node* slices[16]{};
  size_t slice_count = 0;

  const Rope::Node* cur = root;
  uint32_t rem = pos;
  uint32_t leaf_start = 0;

  while (cur->height > 0) {
    const Rope::Node* const* ch = internal_children(cur);
    uint8_t idx = 0;
    while (idx + 1 < cur->count && rem >= ch[idx]->summary.bytes) {
      rem -= ch[idx]->summary.bytes;
      leaf_start += ch[idx]->summary.bytes;
      ++idx;
    }
    uint8_t right_cnt = static_cast<uint8_t>(cur->count - (idx + 1));
    if (right_cnt > 0) {
      const Rope::Node* slice = nullptr;
      if (right_cnt == 1) {
        retain_node(ch[idx + 1]);
        slice = ch[idx + 1];
      } else {
        for (uint8_t k = 0; k < right_cnt; ++k) {
          retain_node(ch[idx + 1 + k]);
        }
        slice = make_internal(ch + idx + 1, right_cnt, alloc);
      }
      slices[slice_count++] = slice;
    }
    cur = ch[idx];
  }

  const Rope::Node* right_tree = nullptr;
  for (size_t k = slice_count; k > 0; --k) {
    right_tree = concat_trees(right_tree, slices[k - 1], alloc);
  }
  retain_node(cur);
  return {right_tree, cur, leaf_start};
}

struct LeafAt {
  const Rope::Node* leaf = nullptr;
  uint32_t leaf_start = 0;
};

LeafAt find_leaf_at(const Rope::Node* root, uint32_t byte_offset) noexcept {
  if (!root || byte_offset >= root->summary.bytes) {
    return {nullptr, 0};
  }
  const Rope::Node* cur = root;
  uint32_t rem = byte_offset;
  uint32_t leaf_start = 0;
  while (cur->height > 0) {
    const Rope::Node* const* ch = internal_children(cur);
    uint8_t idx = 0;
    while (idx + 1 < cur->count && rem >= ch[idx]->summary.bytes) {
      rem -= ch[idx]->summary.bytes;
      leaf_start += ch[idx]->summary.bytes;
      ++idx;
    }
    cur = ch[idx];
  }
  return {cur, leaf_start};
}

bool verify_node_rec(const Rope::Node* n, bool is_root, uint8_t expected_height,
                     size_t total_leaves) noexcept {
  if (!n) {
    return true;
  }
  if (n->height != expected_height) {
    return false;
  }
  if (n->height == 0) {
    const uint32_t len = n->summary.bytes;
    if (len == 0) {
      return false;
    }
    if (total_leaves > 1 && (len < Rope::kMinLeafBytes || len > Rope::kMaxLeafBytes)) {
      return false;
    }
    const uint8_t* data = leaf_data(n);
    // R2: cannot start with UTF-8 continuation byte.
    if (is_utf8_cont(data[0])) {
      return false;
    }
    // R1: leaf summary and poly match actual bytes.
    TextSummary actual = TextSummary::of(std::string_view(reinterpret_cast<const char*>(data), len));
    if (actual != n->summary) {
      return false;
    }
    if (poly_bytes(data, len) != n->poly) {
      return false;
    }
    return true;
  }

  // Internal node fan-out check.
  if (is_root) {
    if (n->count < 2 || n->count > Rope::kMaxInternalChildren) {
      return false;
    }
  } else {
    if (n->count < Rope::kMinInternalChildren || n->count > Rope::kMaxInternalChildren) {
      return false;
    }
  }

  const Rope::Node* const* ch = internal_children(n);
  TextSummary sum{};
  uint64_t poly = 0;
  for (uint8_t i = 0; i < n->count; ++i) {
    if (!verify_node_rec(ch[i], false, static_cast<uint8_t>(n->height - 1), total_leaves)) {
      return false;
    }
    sum += ch[i]->summary;
    poly = poly * pow_base(ch[i]->summary.bytes) + ch[i]->poly;
  }
  if (sum != n->summary || poly != n->poly) {
    return false;
  }
  return true;
}

size_t count_leaves(const Rope::Node* n) noexcept {
  if (!n) {
    return 0;
  }
  if (n->height == 0) {
    return 1;
  }
  size_t total = 0;
  const Rope::Node* const* ch = internal_children(n);
  for (uint8_t i = 0; i < n->count; ++i) {
    total += count_leaves(ch[i]);
  }
  return total;
}

bool check_leaf_boundaries(const Rope::Node* n, uint8_t& prev_last_byte, bool& has_prev) noexcept {
  if (!n) {
    return true;
  }
  if (n->height == 0) {
    const uint8_t* data = leaf_data(n);
    if (has_prev) {
      if (is_utf8_cont(data[0])) {
        return false;
      }
      if (prev_last_byte == '\r' && data[0] == '\n') {
        return false;
      }
    }
    prev_last_byte = data[n->summary.bytes - 1];
    has_prev = true;
    return true;
  }
  const Rope::Node* const* ch = internal_children(n);
  for (uint8_t i = 0; i < n->count; ++i) {
    if (!check_leaf_boundaries(ch[i], prev_last_byte, has_prev)) {
      return false;
    }
  }
  return true;
}

} // namespace

// ---------- Rope ----------

Rope::Rope(const lx_host_api* host) noexcept : alloc_(normalize_alloc(host)) {}

Rope::Rope(const Node* root, Allocator alloc) noexcept
    : root_(root), alloc_(ensure_alloc(alloc)) {}

Rope::~Rope() {
  release_node(root_);
  root_ = nullptr;
}

Rope::Rope(const Rope& other) noexcept : root_(other.root_), alloc_(other.alloc_) {
  retain_node(root_);
}

Rope::Rope(Rope&& other) noexcept : root_(other.root_), alloc_(other.alloc_) {
  other.root_ = nullptr;
}

Rope& Rope::operator=(const Rope& other) noexcept {
  if (this != &other) {
    retain_node(other.root_);
    release_node(root_);
    root_ = other.root_;
    alloc_ = other.alloc_;
  }
  return *this;
}

Rope& Rope::operator=(Rope&& other) noexcept {
  if (this != &other) {
    release_node(root_);
    root_ = other.root_;
    alloc_ = other.alloc_;
    other.root_ = nullptr;
  }
  return *this;
}

Rope Rope::from_utf8(std::string_view text, const lx_host_api* host) {
  Allocator alloc = normalize_alloc(host);
  if (text.empty()) {
    return Rope(nullptr, alloc);
  }
  std::vector<const Node*> leaves;
  chunk_bytes_into_leaves(reinterpret_cast<const uint8_t*>(text.data()),
                          static_cast<uint32_t>(text.size()), true, alloc, leaves);
  const Node* root = build_from_uniform(leaves, alloc);
  return Rope(root, alloc);
}

Rope Rope::splice(uint32_t start, uint32_t end, std::string_view insert_text) const {
  const Allocator alloc = ensure_alloc(alloc_);
  const uint32_t total = byte_length();
  end = std::min(end, total);
  start = std::min(start, end);

  if (start == end && insert_text.empty()) {
    return *this;
  }
  if (!root_ || (start == 0 && end == total)) {
    if (insert_text.empty()) {
      return Rope(nullptr, alloc);
    }
    std::vector<const Node*> leaves;
    chunk_bytes_into_leaves(reinterpret_cast<const uint8_t*>(insert_text.data()),
                            static_cast<uint32_t>(insert_text.size()), true, alloc, leaves);
    return Rope(build_from_uniform(leaves, alloc), alloc);
  }

  const Node* left_prefix = nullptr;
  const Node* right_suffix = nullptr;
  std::string middle;

  if (start > 0) {
    SplitResult left = split_left_of(root_, start, alloc);
    left_prefix = left.tree;
    uint32_t keep = start - left.leaf_start;
    middle.assign(reinterpret_cast<const char*>(leaf_data(left.leaf)), keep);
    release_node(left.leaf);
  }

  middle.append(insert_text);

  if (end < total) {
    SplitResult right = split_right_of(root_, end, alloc);
    right_suffix = right.tree;
    uint32_t skip = end - right.leaf_start;
    uint32_t keep = right.leaf->summary.bytes - skip;
    middle.append(reinterpret_cast<const char*>(leaf_data(right.leaf) + skip), keep);
    release_node(right.leaf);
  }

  auto needs_more_for_split = [&]() -> bool {
    const size_t sz = middle.size();
    if (sz > 0 && sz < kMinLeafBytes) {
      return true;
    }
    if (sz >= 1025 && sz <= 1027) {
      const uint8_t* p = reinterpret_cast<const uint8_t*>(middle.data());
      return find_valid_split(p, static_cast<uint32_t>(sz), kMinLeafBytes,
                              static_cast<uint32_t>(sz) - kMinLeafBytes,
                              static_cast<uint32_t>(sz) / 2) == UINT32_MAX;
    }
    return false;
  };

  while (needs_more_for_split()) {
    if (left_prefix) {
      SplitResult pop = split_left_of(left_prefix, left_prefix->summary.bytes, alloc);
      release_node(left_prefix);
      left_prefix = pop.tree;
      middle.insert(0, reinterpret_cast<const char*>(leaf_data(pop.leaf)),
                    pop.leaf->summary.bytes);
      release_node(pop.leaf);
    } else if (right_suffix) {
      SplitResult pop = split_right_of(right_suffix, 0, alloc);
      release_node(right_suffix);
      right_suffix = pop.tree;
      middle.append(reinterpret_cast<const char*>(leaf_data(pop.leaf)),
                    pop.leaf->summary.bytes);
      release_node(pop.leaf);
    } else {
      break;
    }
  }

  std::vector<const Node*> mid_leaves;
  const bool is_sole = (left_prefix == nullptr && right_suffix == nullptr);
  chunk_bytes_into_leaves(reinterpret_cast<const uint8_t*>(middle.data()),
                          static_cast<uint32_t>(middle.size()), is_sole, alloc, mid_leaves);
  const Node* mid_tree = build_from_uniform(mid_leaves, alloc);
  const Node* joined = concat_trees(left_prefix, mid_tree, alloc);
  joined = concat_trees(joined, right_suffix, alloc);
  return Rope(joined, alloc);
}

RopeView Rope::slice(uint32_t start, uint32_t end) const {
  return RopeView(*this, lx_range{start, end});
}

RopeView Rope::slice(lx_range range) const {
  return RopeView(*this, range);
}

RopeView Rope::view(std::span<const lx_range> ranges) const {
  return RopeView(*this, ranges);
}

TextSummary Rope::summary() const noexcept {
  return root_ ? root_->summary : TextSummary{};
}

uint32_t Rope::byte_length() const noexcept {
  return root_ ? root_->summary.bytes : 0;
}

uint32_t Rope::char_length() const noexcept {
  return root_ ? root_->summary.chars : 0;
}

uint32_t Rope::utf16_length() const noexcept {
  return root_ ? root_->summary.utf16 : 0;
}

uint32_t Rope::newline_count() const noexcept {
  return root_ ? root_->summary.newlines : 0;
}

uint64_t Rope::hash() const noexcept {
  if (!root_) {
    return 0;
  }
  return mix64(root_->poly ^ (static_cast<uint64_t>(root_->summary.bytes) * 0x9e3779b97f4a7c15ULL));
}

Point Rope::byte_to_point(uint32_t byte_offset) const noexcept {
  if (!root_) {
    return {0, 0};
  }
  if (byte_offset >= root_->summary.bytes) {
    return {root_->summary.newlines, root_->summary.last_line_bytes};
  }
  const Node* cur = root_;
  uint32_t rem = byte_offset;
  uint32_t line = 0;
  uint32_t line_bytes = 0;

  while (cur->height > 0) {
    const Node* const* ch = internal_children(cur);
    uint8_t idx = 0;
    while (idx + 1 < cur->count && rem >= ch[idx]->summary.bytes) {
      const TextSummary& cs = ch[idx]->summary;
      rem -= cs.bytes;
      if (cs.newlines == 0) {
        line_bytes += cs.bytes;
      } else {
        line += cs.newlines;
        line_bytes = cs.last_line_bytes;
      }
      ++idx;
    }
    cur = ch[idx];
  }

  const uint8_t* data = leaf_data(cur);
  for (uint32_t i = 0; i < rem; ++i) {
    if (data[i] == '\n') {
      ++line;
      line_bytes = 0;
    } else {
      ++line_bytes;
    }
  }
  return {line, line_bytes};
}

uint32_t Rope::line_start_byte(uint32_t line) const noexcept {
  if (!root_ || line == 0) {
    return 0;
  }
  if (line > root_->summary.newlines) {
    return root_->summary.bytes;
  }
  const Node* cur = root_;
  uint32_t rem_nl = line;
  uint32_t bytes_before = 0;

  while (cur->height > 0) {
    const Node* const* ch = internal_children(cur);
    uint8_t idx = 0;
    while (idx + 1 < cur->count && ch[idx]->summary.newlines < rem_nl) {
      rem_nl -= ch[idx]->summary.newlines;
      bytes_before += ch[idx]->summary.bytes;
      ++idx;
    }
    cur = ch[idx];
  }

  const uint8_t* data = leaf_data(cur);
  const uint32_t len = cur->summary.bytes;
  for (uint32_t i = 0; i < len; ++i) {
    if (data[i] == '\n') {
      if (--rem_nl == 0) {
        return bytes_before + i + 1;
      }
    }
  }
  return root_->summary.bytes;
}

uint32_t Rope::line_end_byte(uint32_t line) const noexcept {
  if (!root_) {
    return 0;
  }
  if (line >= root_->summary.newlines) {
    return root_->summary.bytes;
  }
  return line_start_byte(line + 1) - 1;
}

uint32_t Rope::point_to_byte(Point point) const noexcept {
  uint32_t start = line_start_byte(point.line);
  uint32_t end = line_end_byte(point.line);
  return start + std::min(point.column, end - start);
}

uint32_t Rope::byte_to_utf16(uint32_t byte_offset) const noexcept {
  if (!root_) {
    return 0;
  }
  if (byte_offset >= root_->summary.bytes) {
    return root_->summary.utf16;
  }
  const Node* cur = root_;
  uint32_t rem = byte_offset;
  uint32_t u16 = 0;

  while (cur->height > 0) {
    const Node* const* ch = internal_children(cur);
    uint8_t idx = 0;
    while (idx + 1 < cur->count && rem >= ch[idx]->summary.bytes) {
      rem -= ch[idx]->summary.bytes;
      u16 += ch[idx]->summary.utf16;
      ++idx;
    }
    cur = ch[idx];
  }

  const uint8_t* data = leaf_data(cur);
  const uint32_t len = cur->summary.bytes;
  uint32_t i = 0;
  while (i < rem) {
    Utf8Step step = utf8_step(data + i, len - i);
    if (i + step.byte_len > rem) {
      break;
    }
    u16 += step.utf16_len;
    i += step.byte_len;
  }
  return u16;
}

uint32_t Rope::utf16_to_byte(uint32_t utf16_offset) const noexcept {
  if (!root_) {
    return 0;
  }
  if (utf16_offset >= root_->summary.utf16) {
    return root_->summary.bytes;
  }
  const Node* cur = root_;
  uint32_t rem = utf16_offset;
  uint32_t bytes_before = 0;

  while (cur->height > 0) {
    const Node* const* ch = internal_children(cur);
    uint8_t idx = 0;
    while (idx + 1 < cur->count && rem >= ch[idx]->summary.utf16) {
      rem -= ch[idx]->summary.utf16;
      bytes_before += ch[idx]->summary.bytes;
      ++idx;
    }
    cur = ch[idx];
  }

  const uint8_t* data = leaf_data(cur);
  const uint32_t len = cur->summary.bytes;
  uint32_t i = 0;
  uint32_t u16 = 0;
  while (i < len && u16 < rem) {
    Utf8Step step = utf8_step(data + i, len - i);
    if (u16 + step.utf16_len > rem) {
      break;
    }
    u16 += step.utf16_len;
    i += step.byte_len;
  }
  return bytes_before + i;
}

uint32_t Rope::byte_to_char(uint32_t byte_offset) const noexcept {
  if (!root_) {
    return 0;
  }
  if (byte_offset >= root_->summary.bytes) {
    return root_->summary.chars;
  }
  const Node* cur = root_;
  uint32_t rem = byte_offset;
  uint32_t chars = 0;

  while (cur->height > 0) {
    const Node* const* ch = internal_children(cur);
    uint8_t idx = 0;
    while (idx + 1 < cur->count && rem >= ch[idx]->summary.bytes) {
      rem -= ch[idx]->summary.bytes;
      chars += ch[idx]->summary.chars;
      ++idx;
    }
    cur = ch[idx];
  }

  const uint8_t* data = leaf_data(cur);
  const uint32_t len = cur->summary.bytes;
  uint32_t i = 0;
  while (i < rem) {
    Utf8Step step = utf8_step(data + i, len - i);
    if (i + step.byte_len > rem) {
      break;
    }
    ++chars;
    i += step.byte_len;
  }
  return chars;
}

uint32_t Rope::char_to_byte(uint32_t char_offset) const noexcept {
  if (!root_) {
    return 0;
  }
  if (char_offset >= root_->summary.chars) {
    return root_->summary.bytes;
  }
  const Node* cur = root_;
  uint32_t rem = char_offset;
  uint32_t bytes_before = 0;

  while (cur->height > 0) {
    const Node* const* ch = internal_children(cur);
    uint8_t idx = 0;
    while (idx + 1 < cur->count && rem >= ch[idx]->summary.chars) {
      rem -= ch[idx]->summary.chars;
      bytes_before += ch[idx]->summary.bytes;
      ++idx;
    }
    cur = ch[idx];
  }

  const uint8_t* data = leaf_data(cur);
  const uint32_t len = cur->summary.bytes;
  uint32_t i = 0;
  uint32_t c = 0;
  while (i < len && c < rem) {
    Utf8Step step = utf8_step(data + i, len - i);
    ++c;
    i += step.byte_len;
  }
  return bytes_before + i;
}

ChunkSpan Rope::chunk_at(uint32_t byte_offset) const noexcept {
  LeafAt loc = find_leaf_at(root_, byte_offset);
  if (!loc.leaf) {
    return {nullptr, 0};
  }
  uint32_t off = byte_offset - loc.leaf_start;
  return {leaf_data(loc.leaf) + off, loc.leaf->summary.bytes - off};
}

uint8_t Rope::byte_at(uint32_t byte_offset) const noexcept {
  ChunkSpan cs = chunk_at(byte_offset);
  assert(cs.ptr && cs.len > 0);
  return cs.ptr ? cs.ptr[0] : 0;
}

void Rope::append_to(std::string& out, uint32_t start, uint32_t end) const {
  const uint32_t total = byte_length();
  end = std::min(end, total);
  start = std::min(start, end);
  if (start == end) {
    return;
  }
  out.reserve(out.size() + (end - start));
  uint32_t pos = start;
  while (pos < end) {
    ChunkSpan cs = chunk_at(pos);
    if (!cs.ptr || cs.len == 0) {
      break;
    }
    uint32_t take = std::min(cs.len, end - pos);
    out.append(reinterpret_cast<const char*>(cs.ptr), take);
    pos += take;
  }
}

std::string Rope::slice_text(uint32_t start, uint32_t end) const {
  std::string out;
  append_to(out, start, end);
  return out;
}

std::string Rope::to_string() const {
  std::string out;
  append_to(out, 0, byte_length());
  return out;
}

bool Rope::verify_invariants() const noexcept {
  if (!root_) {
    return true;
  }
  size_t leaves = count_leaves(root_);
  if (leaves == 0) {
    return false;
  }
  if (!verify_node_rec(root_, true, root_->height, leaves)) {
    return false;
  }
  uint8_t prev_last = 0;
  bool has_prev = false;
  return check_leaf_boundaries(root_, prev_last, has_prev);
}

// ---------- RopeView ----------

RopeView::RopeView(Rope rope, lx_range range)
    : RopeView(std::move(rope), std::span<const lx_range>(&range, 1)) {}

RopeView::RopeView(Rope rope, std::span<const lx_range> ranges) : rope_(std::move(rope)) {
  const uint32_t doc_len = rope_.byte_length();
  ranges_.reserve(ranges.size());
  view_starts_.reserve(ranges.size());

  uint32_t acc = 0;
  for (lx_range r : ranges) {
    uint32_t s = std::min(r.start, doc_len);
    uint32_t e = std::min(std::max(r.end, s), doc_len);
    if (s == e) {
      continue;
    }
    ranges_.push_back(lx_range{s, e});
    view_starts_.push_back(acc);
    acc += (e - s);
  }
  total_bytes_ = acc;
}

uint32_t RopeView::view_to_doc(uint32_t view_offset, Assoc assoc) const noexcept {
  if (ranges_.empty()) {
    return 0;
  }
  if (view_offset >= total_bytes_) {
    return ranges_.back().end;
  }
  if (view_offset == 0) {
    return ranges_.front().start;
  }
  // Find first range with view_start > view_offset.
  auto it = std::upper_bound(view_starts_.begin(), view_starts_.end(), view_offset);
  size_t idx = static_cast<size_t>(it - view_starts_.begin()) - 1;
  uint32_t rel = view_offset - view_starts_[idx];
  if (rel == 0 && assoc == Assoc::Before && idx > 0) {
    return ranges_[idx - 1].end;
  }
  return ranges_[idx].start + rel;
}

std::optional<uint32_t> RopeView::doc_to_view(uint32_t doc_offset, Assoc assoc) const noexcept {
  if (ranges_.empty()) {
    return std::nullopt;
  }
  auto it = std::upper_bound(ranges_.begin(), ranges_.end(), doc_offset,
                             [](uint32_t d, const lx_range& r) { return d < r.start; });
  size_t idx = static_cast<size_t>(it - ranges_.begin());
  if (idx > 0) {
    const lx_range& prev = ranges_[idx - 1];
    if (doc_offset < prev.end) {
      return view_starts_[idx - 1] + (doc_offset - prev.start);
    }
    if (doc_offset == prev.end) {
      if (assoc == Assoc::Before) {
        return view_starts_[idx - 1] + (prev.end - prev.start);
      }
      if (idx < ranges_.size() && ranges_[idx].start == doc_offset) {
        return view_starts_[idx];
      }
      return std::nullopt;
    }
  }
  return std::nullopt;
}

ChunkSpan RopeView::chunk_at(uint32_t view_offset) const noexcept {
  if (view_offset >= total_bytes_ || ranges_.empty()) {
    return {nullptr, 0};
  }
  auto it = std::upper_bound(view_starts_.begin(), view_starts_.end(), view_offset);
  size_t idx = static_cast<size_t>(it - view_starts_.begin()) - 1;
  const lx_range& r = ranges_[idx];
  uint32_t rel = view_offset - view_starts_[idx];
  uint32_t doc_off = r.start + rel;
  uint32_t range_rem = r.end - doc_off;

  ChunkSpan cs = rope_.chunk_at(doc_off);
  if (!cs.ptr || cs.len == 0) {
    return {nullptr, 0};
  }
  return {cs.ptr, std::min(cs.len, range_rem)};
}

void RopeView::append_to(std::string& out, uint32_t view_start, uint32_t view_end) const {
  view_end = std::min(view_end, total_bytes_);
  view_start = std::min(view_start, view_end);
  if (view_start == view_end) {
    return;
  }
  out.reserve(out.size() + (view_end - view_start));
  uint32_t pos = view_start;
  while (pos < view_end) {
    ChunkSpan cs = chunk_at(pos);
    if (!cs.ptr || cs.len == 0) {
      break;
    }
    uint32_t take = std::min(cs.len, view_end - pos);
    out.append(reinterpret_cast<const char*>(cs.ptr), take);
    pos += take;
  }
}

std::string RopeView::slice_text(uint32_t view_start, uint32_t view_end) const {
  std::string out;
  append_to(out, view_start, view_end);
  return out;
}

std::string RopeView::to_string() const {
  std::string out;
  append_to(out, 0, total_bytes_);
  return out;
}

// ---------- RopeInput ----------

RopeInput::RopeInput(Rope rope) : is_view_(false), rope_(std::move(rope)) {
  sync_input();
}

RopeInput::RopeInput(RopeView view) : view_(std::move(view)), is_view_(true) {
  sync_input();
}

RopeInput::RopeInput(const RopeInput& other)
    : view_(other.view_),
      is_view_(other.is_view_),
      rope_(other.rope_),
      cached_ptr_(other.cached_ptr_),
      cached_start_(other.cached_start_),
      cached_end_(other.cached_end_),
      cached_range_idx_(other.cached_range_idx_) {
  sync_input();
}

RopeInput::RopeInput(RopeInput&& other) noexcept
    : view_(std::move(other.view_)),
      is_view_(other.is_view_),
      rope_(std::move(other.rope_)),
      cached_ptr_(other.cached_ptr_),
      cached_start_(other.cached_start_),
      cached_end_(other.cached_end_),
      cached_range_idx_(other.cached_range_idx_) {
  other.cached_ptr_ = nullptr;
  other.cached_start_ = 0;
  other.cached_end_ = 0;
  sync_input();
  other.sync_input();
}

RopeInput& RopeInput::operator=(const RopeInput& other) {
  if (this != &other) {
    view_ = other.view_;
    is_view_ = other.is_view_;
    rope_ = other.rope_;
    cached_ptr_ = other.cached_ptr_;
    cached_start_ = other.cached_start_;
    cached_end_ = other.cached_end_;
    cached_range_idx_ = other.cached_range_idx_;
    sync_input();
  }
  return *this;
}

RopeInput& RopeInput::operator=(RopeInput&& other) noexcept {
  if (this != &other) {
    view_ = std::move(other.view_);
    is_view_ = other.is_view_;
    rope_ = std::move(other.rope_);
    cached_ptr_ = other.cached_ptr_;
    cached_start_ = other.cached_start_;
    cached_end_ = other.cached_end_;
    cached_range_idx_ = other.cached_range_idx_;
    other.cached_ptr_ = nullptr;
    other.cached_start_ = 0;
    other.cached_end_ = 0;
    sync_input();
    other.sync_input();
  }
  return *this;
}

void RopeInput::reset(Rope rope) {
  is_view_ = false;
  view_ = RopeView{};
  rope_ = std::move(rope);
  cached_ptr_ = nullptr;
  cached_start_ = 0;
  cached_end_ = 0;
  cached_range_idx_ = 0;
  sync_input();
}

void RopeInput::reset(RopeView view) {
  is_view_ = true;
  rope_ = Rope{};
  view_ = std::move(view);
  cached_ptr_ = nullptr;
  cached_start_ = 0;
  cached_end_ = 0;
  cached_range_idx_ = 0;
  sync_input();
}

void RopeInput::sync_input() noexcept {
  input_.ctx = this;
  input_.length = is_view_ ? view_.byte_length() : rope_.byte_length();
  input_.chunk = &RopeInput::chunk_cb;
}

const uint8_t* LX_CALL RopeInput::chunk_cb(void* ctx, uint32_t offset, uint32_t* len) {
  return static_cast<RopeInput*>(ctx)->get_chunk(offset, len);
}

const uint8_t* RopeInput::get_chunk(uint32_t offset, uint32_t* len) noexcept {
  if (offset >= input_.length) {
    if (len) {
      *len = 0;
    }
    return nullptr;
  }
  if (cached_ptr_ && offset >= cached_start_ && offset < cached_end_) {
    uint32_t diff = offset - cached_start_;
    if (len) {
      *len = cached_end_ - offset;
    }
    return cached_ptr_ + diff;
  }

  if (!is_view_) {
    LeafAt loc = find_leaf_at(rope_.root_, offset);
    if (!loc.leaf) {
      if (len) {
        *len = 0;
      }
      return nullptr;
    }
    cached_ptr_ = leaf_data(loc.leaf);
    cached_start_ = loc.leaf_start;
    cached_end_ = loc.leaf_start + loc.leaf->summary.bytes;
    uint32_t diff = offset - cached_start_;
    if (len) {
      *len = cached_end_ - offset;
    }
    return cached_ptr_ + diff;
  }

  ChunkSpan cs = view_.chunk_at(offset);
  if (!cs.ptr || cs.len == 0) {
    if (len) {
      *len = 0;
    }
    return nullptr;
  }
  cached_ptr_ = cs.ptr;
  cached_start_ = offset;
  cached_end_ = offset + cs.len;
  if (len) {
    *len = cs.len;
  }
  return cs.ptr;
}

} // namespace lx
