#include "engine/common/alloc.h"
#include "engine/common/hash.h"
#include "engine/common/small_vector.h"

#include <atomic>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

std::atomic<int> g_allocs{0};
std::atomic<int> g_frees{0};

void* LX_CALL tracked_alloc(size_t n) {
  g_allocs.fetch_add(1, std::memory_order_relaxed);
  return std::malloc(n);
}

void LX_CALL tracked_free(void* p) {
  if (p) {
    g_frees.fetch_add(1, std::memory_order_relaxed);
  }
  std::free(p);
}

const lx_host_api kTrackedHost = {
    sizeof(lx_host_api),
    LX_ABI_VERSION,
    &tracked_alloc,
    &tracked_free,
    nullptr,
    nullptr,
    nullptr,
};

struct TrackedObj {
  static inline int live_count = 0;
  int value;
  std::string text;

  TrackedObj() : value(0), text("def") { ++live_count; }
  TrackedObj(int v, std::string t) : value(v), text(std::move(t)) { ++live_count; }
  TrackedObj(const TrackedObj& o) : value(o.value), text(o.text) { ++live_count; }
  TrackedObj(TrackedObj&& o) noexcept : value(o.value), text(std::move(o.text)) {
    ++live_count;
  }
  TrackedObj& operator=(const TrackedObj& o) = default;
  TrackedObj& operator=(TrackedObj&& o) noexcept = default;
  ~TrackedObj() { --live_count; }
};

void test_hash() {
  // Known FNV-1a 64-bit / 32-bit reference vectors
  assert(lx::fnv1a_64("") == 0xCBF29CE484222325ULL);
  assert(lx::fnv1a_64("foobar") == 0x85944171F73967E8ULL);
  assert(lx::fnv1a_32("") == 0x811C9DC5u);
  assert(lx::fnv1a_32("foobar") == 0xBF9CF968u);

  uint64_t h1 = lx::hash_combine_64(1, 2);
  uint64_t h2 = lx::hash_combine_64(2, 1);
  assert(h1 != h2);

  uint32_t m1 = lx::mix32(42);
  uint32_t m2 = lx::mix32(43);
  assert(m1 != m2);
}

void test_allocator() {
  g_allocs = 0;
  g_frees = 0;
  lx::Allocator alloc(&kTrackedHost);

  {
    std::vector<int, lx::HostAllocator<int>> v{lx::HostAllocator<int>(alloc)};
    for (int i = 0; i < 64; ++i) {
      v.push_back(i);
    }
    assert(v[63] == 63);
  }
  assert(g_allocs > 0);
  assert(g_allocs == g_frees);

  TrackedObj* obj = alloc.make<TrackedObj>(7, "hello");
  assert(obj && obj->value == 7 && obj->text == "hello");
  assert(TrackedObj::live_count == 1);
  alloc.destroy(obj);
  assert(TrackedObj::live_count == 0);
}

void test_small_vector() {
  g_allocs = 0;
  g_frees = 0;
  lx::Allocator alloc(&kTrackedHost);

  {
    lx::small_vector<TrackedObj, 4> sv(alloc);
    assert(sv.is_inline());
    sv.emplace_back(1, "a");
    sv.emplace_back(2, "b");
    sv.emplace_back(3, "c");
    sv.emplace_back(4, "d");
    assert(sv.is_inline());
    assert(g_allocs == 0);
    assert(TrackedObj::live_count == 4);

    // Spill to heap
    sv.emplace_back(5, "e");
    assert(!sv.is_inline());
    assert(g_allocs == 1);
    assert(sv.size() == 5);
    assert(TrackedObj::live_count == 5);

    // Copy & move
    auto copy = sv;
    assert(copy.size() == 5);
    assert(copy[4].value == 5);
    auto moved = std::move(copy);
    assert(moved.size() == 5);
    assert(moved[0].text == "a");

    moved.erase(moved.begin() + 1);
    assert(moved.size() == 4);
    assert(moved[1].value == 3);
  }
  assert(TrackedObj::live_count == 0);
  assert(g_allocs == g_frees);

  // Inline move
  {
    lx::SmallVector<TrackedObj, 4> a;
    a.emplace_back(10, "x");
    a.emplace_back(20, "y");
    lx::SmallVector<TrackedObj, 4> b = std::move(a);
    assert(b.is_inline());
    assert(b.size() == 2);
    assert(b[1].value == 20);
    b.resize(4);
    assert(b.size() == 4);
    assert(b[3].text == "def");
  }
  assert(TrackedObj::live_count == 0);
}

}  // namespace

int main() {
  test_hash();
  test_allocator();
  test_small_vector();
  return 0;
}
