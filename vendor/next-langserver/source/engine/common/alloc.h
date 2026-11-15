#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

#include "lx/lx_language.h"

namespace lx {

/// Default CRT-backed host allocation functions when no custom lx_host_api is supplied.
const lx_host_api& default_host_api() noexcept;

/// Thin non-owning wrapper around lx_host_api alloc/free with safe CRT fallback.
class Allocator {
 public:
  constexpr Allocator() noexcept = default;
  explicit constexpr Allocator(const lx_host_api* host) noexcept : host_(host) {}

  [[nodiscard]] void* alloc(size_t bytes) const noexcept {
    if (bytes == 0) {
      bytes = 1;
    }
    if (host_ && host_->alloc) {
      return host_->alloc(bytes);
    }
    return default_host_api().alloc(bytes);
  }

  void free(void* ptr) const noexcept {
    if (!ptr) {
      return;
    }
    if (host_ && host_->free) {
      host_->free(ptr);
      return;
    }
    default_host_api().free(ptr);
  }

  template <typename T>
  [[nodiscard]] T* alloc_array(size_t count) const noexcept {
    if (count == 0) {
      return nullptr;
    }
    if (count > (SIZE_MAX / sizeof(T))) {
      return nullptr;
    }
    return static_cast<T*>(alloc(count * sizeof(T)));
  }

  template <typename T, typename... Args>
  [[nodiscard]] T* make(Args&&... args) const {
    void* mem = alloc(sizeof(T));
    if (!mem) {
      return nullptr;
    }
    if constexpr (std::is_nothrow_constructible_v<T, Args&&...>) {
      return ::new (mem) T(std::forward<Args>(args)...);
    } else {
      try {
        return ::new (mem) T(std::forward<Args>(args)...);
      } catch (...) {
        free(mem);
        throw;
      }
    }
  }

  template <typename T>
  void destroy(T* ptr) const noexcept {
    if (!ptr) {
      return;
    }
    ptr->~T();
    free(ptr);
  }

  [[nodiscard]] constexpr const lx_host_api* host() const noexcept { return host_; }

  [[nodiscard]] friend constexpr bool operator==(const Allocator& a,
                                                 const Allocator& b) noexcept {
    return a.host_ == b.host_;
  }

 private:
  const lx_host_api* host_ = nullptr;
};

/// STL-compatible allocator backed by lx::Allocator (routes through lx_host_api).
template <typename T>
class HostAllocator {
 public:
  using value_type = T;
  using propagate_on_container_copy_assignment = std::true_type;
  using propagate_on_container_move_assignment = std::true_type;
  using propagate_on_container_swap = std::true_type;

  constexpr HostAllocator() noexcept = default;
  explicit constexpr HostAllocator(Allocator alloc) noexcept : alloc_(alloc) {}
  explicit constexpr HostAllocator(const lx_host_api* host) noexcept : alloc_(host) {}

  template <typename U>
  constexpr HostAllocator(const HostAllocator<U>& other) noexcept
      : alloc_(other.allocator()) {}

  [[nodiscard]] T* allocate(size_t n) const {
    T* p = alloc_.alloc_array<T>(n);
    if (!p && n > 0) {
      throw std::bad_alloc();
    }
    return p;
  }

  void deallocate(T* p, size_t) const noexcept { alloc_.free(p); }

  [[nodiscard]] constexpr Allocator allocator() const noexcept { return alloc_; }

  template <typename U>
  [[nodiscard]] friend constexpr bool operator==(const HostAllocator& a,
                                                 const HostAllocator<U>& b) noexcept {
    return a.alloc_ == b.allocator();
  }

 private:
  Allocator alloc_;
};

}  // namespace lx
