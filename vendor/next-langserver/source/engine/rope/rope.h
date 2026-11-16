#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "lx/lx_language.h"

namespace lx {

/// 0-based line and UTF-8 byte column within a document (§4.2).
struct Point {
  uint32_t line = 0;
  uint32_t column = 0;

  bool operator==(const Point&) const noexcept = default;
};

/// Monoidal aggregate metrics for a UTF-8 text span (§4.1).
/// Summary(A) + Summary(B) == Summary(AB).
struct TextSummary {
  uint32_t bytes = 0;
  uint32_t chars = 0;            // Unicode codepoints
  uint32_t utf16 = 0;            // UTF-16 code units (for LSP)
  uint32_t newlines = 0;         // count of '\n'
  uint32_t last_line_bytes = 0;  // bytes after the last '\n'
  uint32_t first_line_bytes = 0; // bytes before the first '\n' (== bytes if newlines == 0)

  static TextSummary of(std::string_view utf8) noexcept;

  TextSummary operator+(const TextSummary& rhs) const noexcept;
  TextSummary& operator+=(const TextSummary& rhs) noexcept;

  bool operator==(const TextSummary&) const noexcept = default;
};

/// Disambiguates positions sitting directly on edit or view-range boundaries (§4.4).
enum class Assoc : uint8_t {
  Before = 0,
  After = 1,
};

/// Single replacement in old-text byte coordinates: [old_start, old_end) -> new_len bytes.
struct Hunk {
  uint32_t old_start = 0;
  uint32_t old_end = 0;
  uint32_t new_len = 0;

  bool operator==(const Hunk&) const noexcept = default;
};

/// Sorted, non-overlapping, canonical sequence of hunks in old-text coordinates (§4.4).
class ChangeSet {
 public:
  ChangeSet() = default;

  static ChangeSet single(uint32_t old_start, uint32_t old_end, uint32_t new_len);
  static ChangeSet from_hunks(std::span<const Hunk> hunks);

  bool empty() const noexcept { return hunks_.empty(); }
  std::span<const Hunk> hunks() const noexcept { return hunks_; }

  /// Maps a byte offset from old text to new text (§4.4).
  uint32_t map_pos(uint32_t old_pos, Assoc assoc = Assoc::After) const noexcept;

  /// Maps a byte offset from new text back to old text (§4.4, B.2).
  /// Returns std::nullopt inside newly inserted/replaced bytes.
  /// At a zero-length deletion boundary, Assoc::After yields old_end and Assoc::Before yields old_start.
  std::optional<uint32_t> unmap_pos(uint32_t new_pos, Assoc assoc = Assoc::After) const noexcept;

  /// Composes `this` (V0 -> V1) followed by `next` (V1 -> V2) into (V0 -> V2) (§4.4, I6).
  ChangeSet compose(const ChangeSet& next) const;

  /// Returns true iff [x_start, x_end) in old coordinates is clean wrt all hunks (§5.5).
  bool is_clean(uint32_t x_start, uint32_t x_end) const noexcept;

  int64_t net_delta() const noexcept;
  uint32_t new_length(uint32_t old_length) const noexcept;
  uint32_t old_changed_bytes() const noexcept;
  uint32_t new_changed_bytes() const noexcept;
  double change_ratio(uint32_t old_length) const noexcept;

  bool operator==(const ChangeSet&) const noexcept = default;

 private:
  explicit ChangeSet(std::vector<Hunk> hunks) noexcept : hunks_(std::move(hunks)) {}
  static void normalize(std::vector<Hunk>& hunks);

  std::vector<Hunk> hunks_;
};

/// Contiguous byte slice returned by Rope::chunk_at / RopeView::chunk_at (§4.2).
struct ChunkSpan {
  const uint8_t* ptr = nullptr;
  uint32_t len = 0;
};

class RopeView;

/// Persistent (immutable, CoW, atomically refcounted) B+-tree of UTF-8 chunks (§4).
class Rope {
 public:
  static constexpr uint32_t kMinLeafBytes = 512;
  static constexpr uint32_t kMaxLeafBytes = 1024;
  static constexpr uint32_t kMinInternalChildren = 8;
  static constexpr uint32_t kMaxInternalChildren = 16;

  Rope() noexcept = default;
  explicit Rope(const lx_host_api* host) noexcept;
  ~Rope();

  Rope(const Rope& other) noexcept;
  Rope(Rope&& other) noexcept;
  Rope& operator=(const Rope& other) noexcept;
  Rope& operator=(Rope&& other) noexcept;

  static Rope from_utf8(std::string_view text, const lx_host_api* host = nullptr);

  Rope snapshot() const noexcept { return *this; }

  /// Returns a new Rope with [start, end) replaced by `insert_text` in O(log n + |insert_text|).
  Rope splice(uint32_t start, uint32_t end, std::string_view insert_text) const;
  Rope splice(lx_range range, std::string_view insert_text) const {
    return splice(range.start, range.end, insert_text);
  }

  /// Zero-copy view over one or more byte ranges (§4.2, §4.5).
  RopeView slice(uint32_t start, uint32_t end) const;
  RopeView slice(lx_range range) const;
  RopeView view(std::span<const lx_range> ranges) const;

  TextSummary summary() const noexcept;
  uint32_t byte_length() const noexcept;
  uint32_t char_length() const noexcept;
  uint32_t utf16_length() const noexcept;
  uint32_t newline_count() const noexcept;
  uint32_t line_count() const noexcept { return newline_count() + 1; }
  bool empty() const noexcept { return byte_length() == 0; }

  /// O(1) content hash stored at the root (independent of tree shape).
  uint64_t hash() const noexcept;

  /// Coordinate conversions in O(log n) (§4.2).
  Point byte_to_point(uint32_t byte_offset) const noexcept;
  uint32_t point_to_byte(Point point) const noexcept;

  uint32_t byte_to_utf16(uint32_t byte_offset) const noexcept;
  uint32_t utf16_to_byte(uint32_t utf16_offset) const noexcept;

  uint32_t byte_to_char(uint32_t byte_offset) const noexcept;
  uint32_t char_to_byte(uint32_t char_offset) const noexcept;

  uint32_t line_start_byte(uint32_t line) const noexcept;
  uint32_t line_end_byte(uint32_t line) const noexcept;

  /// Returns contiguous bytes starting at `byte_offset` within its leaf chunk.
  /// Returns {nullptr, 0} when byte_offset >= byte_length().
  ChunkSpan chunk_at(uint32_t byte_offset) const noexcept;

  uint8_t byte_at(uint32_t byte_offset) const noexcept;

  void append_to(std::string& out, uint32_t start = 0, uint32_t end = UINT32_MAX) const;
  std::string slice_text(uint32_t start, uint32_t end) const;
  std::string to_string() const;

  /// Verifies invariants R1-R4 (intended for tests and diagnostics).
  bool verify_invariants() const noexcept;

  struct Node;
  struct Allocator {
    void* (LX_CALL *alloc)(size_t) = nullptr;
    void  (LX_CALL *free)(void*) = nullptr;
  };

 private:
  friend class RopeInput;

  Rope(const Node* root, Allocator alloc) noexcept;

  const Node* root_ = nullptr;
  Allocator alloc_{};
};

/// Virtual concatenation of one or more ranges of a Rope without copying (§4.5, §9).
class RopeView {
 public:
  RopeView() = default;
  RopeView(Rope rope, lx_range range);
  RopeView(Rope rope, std::span<const lx_range> ranges);

  const Rope& rope() const noexcept { return rope_; }
  bool empty() const noexcept { return total_bytes_ == 0; }
  uint32_t byte_length() const noexcept { return total_bytes_; }
  std::span<const lx_range> ranges() const noexcept { return ranges_; }

  uint32_t view_to_doc(uint32_t view_offset, Assoc assoc = Assoc::After) const noexcept;
  std::optional<uint32_t> doc_to_view(uint32_t doc_offset, Assoc assoc = Assoc::After) const noexcept;

  ChunkSpan chunk_at(uint32_t view_offset) const noexcept;

  void append_to(std::string& out, uint32_t view_start = 0, uint32_t view_end = UINT32_MAX) const;
  std::string slice_text(uint32_t view_start, uint32_t view_end) const;
  std::string to_string() const;

 private:
  struct Entry {
    lx_range doc_range{};
    uint32_t view_start = 0;
  };

  Rope rope_{};
  std::vector<lx_range> ranges_;
  std::vector<uint32_t> view_starts_;
  uint32_t total_bytes_ = 0;
};

/// Adapter exposing a Rope or RopeView as `lx_input` with amortized O(1) sequential chunking (§4.2, §4.3).
class RopeInput {
 public:
  explicit RopeInput(Rope rope);
  explicit RopeInput(RopeView view);

  RopeInput(const RopeInput& other);
  RopeInput(RopeInput&& other) noexcept;
  RopeInput& operator=(const RopeInput& other);
  RopeInput& operator=(RopeInput&& other) noexcept;
  ~RopeInput() = default;

  void reset(Rope rope);
  void reset(RopeView view);

  const lx_input* input() const noexcept { return &input_; }
  lx_input* input() noexcept { return &input_; }
  uint32_t length() const noexcept { return input_.length; }

 private:
  static const uint8_t* LX_CALL chunk_cb(void* ctx, uint32_t offset, uint32_t* len);
  const uint8_t* get_chunk(uint32_t offset, uint32_t* len) noexcept;
  void sync_input() noexcept;

  RopeView view_{};
  bool is_view_ = false;
  Rope rope_{};

  // Sequential read cache (valid while `view_` / `rope_` is unchanged).
  const uint8_t* cached_ptr_ = nullptr;
  uint32_t cached_start_ = 0;
  uint32_t cached_end_ = 0;
  size_t cached_range_idx_ = 0;

  lx_input input_{};
};

} // namespace lx
