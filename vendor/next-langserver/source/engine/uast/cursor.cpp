#include "engine/uast/uast.h"

namespace lx::uast {

Cursor::Cursor(GreenPtr root) noexcept : root_(std::move(root)) {
  reset();
}

Cursor::Cursor(const GreenNode* root) noexcept : root_(root) {
  reset();
}

void Cursor::reset() noexcept {
  path_.clear();
  if (root_) {
    path_.push_back(Frame{root_.get(), 0, 0, 0, kNoField});
    if (path_.back().node->is_seq()) {
      descend_first_non_seq();
    }
  }
}

void Cursor::push_child(const GreenInner* parent_inner, uint32_t child_idx,
                        uint32_t child_byte_off, uint32_t child_tok_off) noexcept {
  const GreenRef& ref = parent_inner->child(child_idx);
  FieldId fid = ref.field_id;
  if (fid == kNoField && parent_inner->is_seq() && !path_.empty()) {
    fid = path_.back().field_id;
  }
  path_.push_back(Frame{ref.node, child_idx, child_byte_off, child_tok_off, fid});
}

bool Cursor::descend_first_non_seq() noexcept {
  while (!path_.empty() && path_.back().node->is_seq()) {
    const auto* seq = path_.back().node->as_inner();
    if (seq->child_count == 0) return false;
    const uint32_t off = path_.back().offset;
    const uint32_t tok = path_.back().token_offset;
    push_child(seq, 0, off, tok);
  }
  return !path_.empty() && !path_.back().node->is_seq();
}

bool Cursor::descend_last_non_seq() noexcept {
  while (!path_.empty() && path_.back().node->is_seq()) {
    const auto* seq = path_.back().node->as_inner();
    if (seq->child_count == 0) return false;
    const uint32_t last_idx = seq->child_count - 1;
    const GreenNode* last_child = seq->child(last_idx).node;
    const uint32_t off = path_.back().offset + seq->width - last_child->width;
    const uint32_t tok = path_.back().token_offset + seq->token_count - last_child->get_token_count();
    push_child(seq, last_idx, off, tok);
  }
  return !path_.empty() && !path_.back().node->is_seq();
}

bool Cursor::first_child() noexcept {
  if (path_.empty() || path_.back().node->is_token()) return false;
  const auto* inner = path_.back().node->as_inner();
  if (inner->child_count == 0) return false;

  const size_t saved = path_.size();
  const uint32_t off = path_.back().offset;
  const uint32_t tok = path_.back().token_offset;
  push_child(inner, 0, off, tok);
  if (!descend_first_non_seq()) {
    path_.resize(saved);
    return false;
  }
  return true;
}

bool Cursor::last_child() noexcept {
  if (path_.empty() || path_.back().node->is_token()) return false;
  const auto* inner = path_.back().node->as_inner();
  if (inner->child_count == 0) return false;

  const size_t saved = path_.size();
  const uint32_t last_idx = inner->child_count - 1;
  const GreenNode* last_c = inner->child(last_idx).node;
  const uint32_t off = path_.back().offset + inner->width - last_c->width;
  const uint32_t tok = path_.back().token_offset + inner->token_count - last_c->get_token_count();
  push_child(inner, last_idx, off, tok);
  if (!descend_last_non_seq()) {
    path_.resize(saved);
    return false;
  }
  return true;
}

bool Cursor::next_sibling() noexcept {
  for (size_t k = path_.size(); k > 1; --k) {
    const size_t cur_idx = k - 1;
    const Frame& parent_frame = path_[cur_idx - 1];
    const auto* parent_inner = parent_frame.node->as_inner();
    const uint32_t next_c = path_[cur_idx].child_idx + 1;
    if (next_c < parent_inner->child_count) {
      const uint32_t next_off = path_[cur_idx].offset + path_[cur_idx].node->width;
      const uint32_t next_tok = path_[cur_idx].token_offset + path_[cur_idx].node->get_token_count();
      const size_t saved = path_.size();
      path_.resize(cur_idx);
      push_child(parent_inner, next_c, next_off, next_tok);
      if (descend_first_non_seq()) {
        return true;
      }
      path_.resize(saved);
      return false;
    }
    if (!parent_inner->is_seq()) {
      return false;
    }
  }
  return false;
}

bool Cursor::prev_sibling() noexcept {
  for (size_t k = path_.size(); k > 1; --k) {
    const size_t cur_idx = k - 1;
    const Frame& parent_frame = path_[cur_idx - 1];
    const auto* parent_inner = parent_frame.node->as_inner();
    if (path_[cur_idx].child_idx > 0) {
      const uint32_t prev_c = path_[cur_idx].child_idx - 1;
      const GreenNode* prev_node = parent_inner->child(prev_c).node;
      const uint32_t prev_off = path_[cur_idx].offset - prev_node->width;
      const uint32_t prev_tok = path_[cur_idx].token_offset - prev_node->get_token_count();
      const size_t saved = path_.size();
      path_.resize(cur_idx);
      push_child(parent_inner, prev_c, prev_off, prev_tok);
      if (descend_last_non_seq()) {
        return true;
      }
      path_.resize(saved);
      return false;
    }
    if (!parent_inner->is_seq()) {
      return false;
    }
  }
  return false;
}

bool Cursor::parent() noexcept {
  for (size_t k = path_.size(); k > 1; --k) {
    if (!path_[k - 2].node->is_seq()) {
      path_.resize(k - 1);
      return true;
    }
  }
  return false;
}

bool Cursor::next_leaf() noexcept {
  if (path_.empty()) return false;

  // If current node has children, descend to leftmost leaf inside it.
  if (first_child()) {
    while (first_child()) {}
    return true;
  }

  for (size_t k = path_.size(); k > 1; --k) {
    const size_t cur_idx = k - 1;
    const auto* parent_inner = path_[cur_idx - 1].node->as_inner();
    const uint32_t next_c = path_[cur_idx].child_idx + 1;
    if (next_c < parent_inner->child_count) {
      const uint32_t next_off = path_[cur_idx].offset + path_[cur_idx].node->width;
      const uint32_t next_tok = path_[cur_idx].token_offset + path_[cur_idx].node->get_token_count();
      path_.resize(cur_idx);
      push_child(parent_inner, next_c, next_off, next_tok);
      descend_first_non_seq();
      while (first_child()) {}
      return true;
    }
  }
  return false;
}

bool Cursor::prev_leaf() noexcept {
  if (path_.empty()) return false;

  for (size_t k = path_.size(); k > 1; --k) {
    const size_t cur_idx = k - 1;
    const auto* parent_inner = path_[cur_idx - 1].node->as_inner();
    if (path_[cur_idx].child_idx > 0) {
      const uint32_t prev_c = path_[cur_idx].child_idx - 1;
      const GreenNode* prev_node = parent_inner->child(prev_c).node;
      const uint32_t prev_off = path_[cur_idx].offset - prev_node->width;
      const uint32_t prev_tok = path_[cur_idx].token_offset - prev_node->get_token_count();
      path_.resize(cur_idx);
      push_child(parent_inner, prev_c, prev_off, prev_tok);
      descend_last_non_seq();
      while (last_child()) {}
      return true;
    }
  }
  return false;
}

bool Cursor::goto_byte(uint32_t byte_offset) noexcept {
  if (!root_) return false;
  if (root_->width == 0) {
    if (byte_offset != 0) return false;
    reset();
    return true;
  }
  if (byte_offset >= root_->width) return false;

  if (path_.empty()) reset();

  // Climb up to LCA covering byte_offset (gives O(1) amortized for sequential queries).
  while (path_.size() > 1) {
    const Frame& top = path_.back();
    if (byte_offset >= top.offset && byte_offset < top.offset + top.node->width) {
      break;
    }
    path_.pop_back();
  }

  // Descend to deepest node covering byte_offset.
  while (path_.back().node->is_inner()) {
    const auto* inner = path_.back().node->as_inner();
    uint32_t cur_off = path_.back().offset;
    uint32_t cur_tok = path_.back().token_offset;
    bool descended = false;
    for (uint32_t i = 0; i < inner->child_count; ++i) {
      const GreenNode* c = inner->child(i).node;
      if (c && c->width > 0 && byte_offset >= cur_off && byte_offset < cur_off + c->width) {
        push_child(inner, i, cur_off, cur_tok);
        descended = true;
        break;
      }
      if (c) {
        cur_off += c->width;
        cur_tok += c->get_token_count();
      }
    }
    if (!descended) break;
  }

  while (path_.size() > 1 && path_.back().node->is_seq()) {
    path_.pop_back();
  }
  return !path_.empty();
}

bool Cursor::goto_token(uint32_t token_index) noexcept {
  if (!root_ || token_index >= root_->get_token_count()) return false;
  if (path_.empty()) reset();

  while (path_.size() > 1) {
    const Frame& top = path_.back();
    const uint32_t tc = top.node->get_token_count();
    if (token_index >= top.token_offset && token_index < top.token_offset + tc) {
      break;
    }
    path_.pop_back();
  }

  while (path_.back().node->is_inner()) {
    const auto* inner = path_.back().node->as_inner();
    uint32_t cur_off = path_.back().offset;
    uint32_t cur_tok = path_.back().token_offset;
    bool descended = false;
    for (uint32_t i = 0; i < inner->child_count; ++i) {
      const GreenNode* c = inner->child(i).node;
      const uint32_t c_tc = c ? c->get_token_count() : 0;
      if (c && c_tc > 0 && token_index < cur_tok + c_tc) {
        push_child(inner, i, cur_off, cur_tok);
        descended = true;
        break;
      }
      if (c) {
        cur_off += c->width;
        cur_tok += c_tc;
      }
    }
    if (!descended) break;
  }

  while (path_.size() > 1 && path_.back().node->is_seq()) {
    path_.pop_back();
  }
  return !path_.empty();
}

Kind Cursor::kind() const noexcept {
  return path_.empty() ? 0u : path_.back().node->kind;
}

FieldId Cursor::field() const noexcept {
  return path_.empty() ? kNoField : path_.back().field_id;
}

Range Cursor::range() const noexcept {
  if (path_.empty()) return {};
  const Frame& f = path_.back();
  return Range{f.offset, f.offset + f.node->width};
}

uint32_t Cursor::width() const noexcept {
  return path_.empty() ? 0u : path_.back().node->width;
}

uint32_t Cursor::reach_ahead() const noexcept {
  return path_.empty() ? 0u : path_.back().node->reach_ahead;
}

uint32_t Cursor::flags() const noexcept {
  return path_.empty() ? 0u : path_.back().node->flags;
}

uint32_t Cursor::token_offset() const noexcept {
  return path_.empty() ? 0u : path_.back().token_offset;
}

uint32_t Cursor::token_count() const noexcept {
  return path_.empty() ? 0u : path_.back().node->get_token_count();
}

uint32_t Cursor::prec() const noexcept {
  if (path_.empty() || !path_.back().node->is_inner()) return 0u;
  return path_.back().node->as_inner()->prec;
}

uint32_t Cursor::ctx_depth() const noexcept {
  if (path_.empty() || !path_.back().node->is_inner()) return 0u;
  return path_.back().node->as_inner()->ctx_depth;
}

uint32_t Cursor::ctx_hash() const noexcept {
  if (path_.empty() || !path_.back().node->is_inner()) return 0u;
  return path_.back().node->as_inner()->ctx_hash;
}

uint32_t Cursor::pred_deps_hash() const noexcept {
  if (path_.empty() || !path_.back().node->is_inner()) return 0u;
  return path_.back().node->as_inner()->pred_deps_hash;
}

uint32_t Cursor::struct_hash() const noexcept {
  if (path_.empty() || !path_.back().node->is_inner()) return 0u;
  return path_.back().node->as_inner()->struct_hash;
}

bool Cursor::is_token() const noexcept {
  return !path_.empty() && path_.back().node->is_token();
}

const GreenNode* Cursor::green_node() const noexcept {
  return path_.empty() ? nullptr : path_.back().node;
}

const GreenToken* Cursor::as_token() const noexcept {
  return path_.empty() ? nullptr : path_.back().node->as_token();
}

const GreenInner* Cursor::as_inner() const noexcept {
  return path_.empty() ? nullptr : path_.back().node->as_inner();
}

} // namespace lx::uast
