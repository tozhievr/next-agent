#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <new>
#include <span>
#include <type_traits>
#include <utility>

#include "engine/common/alloc.h"

namespace lx {

/// Inline-buffered vector with N inline elements and heap fallback via lx::Allocator.
template <typename T, size_t N>
class SmallVector {
  static_assert(N > 0, "SmallVector inline capacity N must be > 0");

 public:
  using value_type = T;
  using size_type = size_t;
  using difference_type = ptrdiff_t;
  using reference = T&;
  using const_reference = const T&;
  using pointer = T*;
  using const_pointer = const T*;
  using iterator = T*;
  using const_iterator = const T*;

  SmallVector() noexcept = default;

  explicit SmallVector(Allocator alloc) noexcept : alloc_(alloc) {}

  explicit SmallVector(size_t count, Allocator alloc = {}) : alloc_(alloc) {
    resize(count);
  }

  SmallVector(size_t count, const T& value, Allocator alloc = {}) : alloc_(alloc) {
    assign(count, value);
  }

  SmallVector(std::initializer_list<T> init, Allocator alloc = {}) : alloc_(alloc) {
    reserve(init.size());
    for (const T& v : init) {
      push_back(v);
    }
  }

  SmallVector(const SmallVector& other) : alloc_(other.alloc_) {
    reserve(other.size_);
    for (size_t i = 0; i < other.size_; ++i) {
      emplace_back(other[i]);
    }
  }

  SmallVector(SmallVector&& other) noexcept(std::is_nothrow_move_constructible_v<T>)
      : alloc_(other.alloc_) {
    move_from(std::move(other));
  }

  ~SmallVector() {
    clear();
    if (!is_inline()) {
      alloc_.free(data_);
    }
  }

  SmallVector& operator=(const SmallVector& other) {
    if (this == &other) {
      return *this;
    }
    clear();
    reserve(other.size_);
    for (size_t i = 0; i < other.size_; ++i) {
      emplace_back(other[i]);
    }
    return *this;
  }

  SmallVector& operator=(SmallVector&& other) noexcept(
      std::is_nothrow_move_constructible_v<T>) {
    if (this == &other) {
      return *this;
    }
    clear();
    if (!is_inline()) {
      alloc_.free(data_);
      data_ = inline_ptr();
      capacity_ = N;
    }
    alloc_ = other.alloc_;
    move_from(std::move(other));
    return *this;
  }

  void assign(size_t count, const T& value) {
    clear();
    reserve(count);
    for (size_t i = 0; i < count; ++i) {
      push_back(value);
    }
  }

  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
  [[nodiscard]] size_t size() const noexcept { return size_; }
  [[nodiscard]] size_t capacity() const noexcept { return capacity_; }
  [[nodiscard]] bool is_inline() const noexcept { return data_ == inline_ptr(); }

  [[nodiscard]] T* data() noexcept { return data_; }
  [[nodiscard]] const T* data() const noexcept { return data_; }

  [[nodiscard]] iterator begin() noexcept { return data_; }
  [[nodiscard]] const_iterator begin() const noexcept { return data_; }
  [[nodiscard]] const_iterator cbegin() const noexcept { return data_; }
  [[nodiscard]] iterator end() noexcept { return data_ + size_; }
  [[nodiscard]] const_iterator end() const noexcept { return data_ + size_; }
  [[nodiscard]] const_iterator cend() const noexcept { return data_ + size_; }

  [[nodiscard]] T& operator[](size_t idx) noexcept {
    assert(idx < size_);
    return data_[idx];
  }

  [[nodiscard]] const T& operator[](size_t idx) const noexcept {
    assert(idx < size_);
    return data_[idx];
  }

  [[nodiscard]] T& front() noexcept {
    assert(size_ > 0);
    return data_[0];
  }

  [[nodiscard]] const T& front() const noexcept {
    assert(size_ > 0);
    return data_[0];
  }

  [[nodiscard]] T& back() noexcept {
    assert(size_ > 0);
    return data_[size_ - 1];
  }

  [[nodiscard]] const T& back() const noexcept {
    assert(size_ > 0);
    return data_[size_ - 1];
  }

  [[nodiscard]] operator std::span<T>() noexcept { return {data_, size_}; }
  [[nodiscard]] operator std::span<const T>() const noexcept { return {data_, size_}; }
  [[nodiscard]] std::span<T> span() noexcept { return {data_, size_}; }
  [[nodiscard]] std::span<const T> span() const noexcept { return {data_, size_}; }

  void reserve(size_t new_cap) {
    if (new_cap <= capacity_) {
      return;
    }
    grow_to(new_cap);
  }

  void clear() noexcept {
    if constexpr (!std::is_trivially_destructible_v<T>) {
      std::destroy_n(data_, size_);
    }
    size_ = 0;
  }

  void push_back(const T& value) { emplace_back(value); }
  void push_back(T&& value) { emplace_back(std::move(value)); }

  template <typename... Args>
  T& emplace_back(Args&&... args) {
    if (size_ == capacity_) {
      grow_to(capacity_ * 2);
    }
    T* slot = ::new (static_cast<void*>(data_ + size_)) T(std::forward<Args>(args)...);
    ++size_;
    return *slot;
  }

  void pop_back() noexcept {
    assert(size_ > 0);
    --size_;
    if constexpr (!std::is_trivially_destructible_v<T>) {
      data_[size_].~T();
    }
  }

  void resize(size_t new_size) {
    if (new_size < size_) {
      if constexpr (!std::is_trivially_destructible_v<T>) {
        std::destroy_n(data_ + new_size, size_ - new_size);
      }
      size_ = new_size;
      return;
    }
    if (new_size > capacity_) {
      grow_to(std::max(new_size, capacity_ * 2));
    }
    for (; size_ < new_size; ++size_) {
      ::new (static_cast<void*>(data_ + size_)) T();
    }
  }

  void resize(size_t new_size, const T& value) {
    if (new_size < size_) {
      if constexpr (!std::is_trivially_destructible_v<T>) {
        std::destroy_n(data_ + new_size, size_ - new_size);
      }
      size_ = new_size;
      return;
    }
    if (new_size > capacity_) {
      grow_to(std::max(new_size, capacity_ * 2));
    }
    for (; size_ < new_size; ++size_) {
      ::new (static_cast<void*>(data_ + size_)) T(value);
    }
  }

  iterator erase(const_iterator pos) {
    assert(pos >= data_ && pos < data_ + size_);
    size_t idx = static_cast<size_t>(pos - data_);
    for (size_t i = idx; i + 1 < size_; ++i) {
      data_[i] = std::move(data_[i + 1]);
    }
    pop_back();
    return data_ + idx;
  }

 private:
  [[nodiscard]] T* inline_ptr() noexcept {
    return std::launder(reinterpret_cast<T*>(inline_storage_));
  }
  [[nodiscard]] const T* inline_ptr() const noexcept {
    return std::launder(reinterpret_cast<const T*>(inline_storage_));
  }

  void grow_to(size_t new_cap) {
    T* new_buf = alloc_.alloc_array<T>(new_cap);
    if (!new_buf) {
      throw std::bad_alloc();
    }
    if constexpr (std::is_nothrow_move_constructible_v<T>) {
      std::uninitialized_move_n(data_, size_, new_buf);
    } else {
      try {
        std::uninitialized_copy_n(data_, size_, new_buf);
      } catch (...) {
        alloc_.free(new_buf);
        throw;
      }
    }
    if constexpr (!std::is_trivially_destructible_v<T>) {
      std::destroy_n(data_, size_);
    }
    if (!is_inline()) {
      alloc_.free(data_);
    }
    data_ = new_buf;
    capacity_ = new_cap;
  }

  void move_from(SmallVector&& other) {
    if (!other.is_inline()) {
      data_ = other.data_;
      size_ = other.size_;
      capacity_ = other.capacity_;
      other.data_ = other.inline_ptr();
      other.size_ = 0;
      other.capacity_ = N;
      return;
    }
    std::uninitialized_move_n(other.data_, other.size_, data_);
    size_ = other.size_;
    other.clear();
  }

  Allocator alloc_{};
  T* data_ = inline_ptr();
  size_t size_ = 0;
  size_t capacity_ = N;
  alignas(T) std::byte inline_storage_[sizeof(T) * N];
};

template <typename T, size_t N>
using small_vector = SmallVector<T, N>;

}  // namespace lx
