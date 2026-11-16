#include "engine/common/alloc.h"

#include <cstdlib>

namespace lx {
namespace {

void* LX_CALL crt_alloc(size_t n) {
  return std::malloc(n);
}

void LX_CALL crt_free(void* p) {
  std::free(p);
}

void LX_CALL crt_log(int, lx_str) {}

uint64_t LX_CALL crt_state_intern(lx_str) {
  return 0;
}

lx_str LX_CALL crt_state_resolve(uint64_t) {
  return lx_str{nullptr, 0};
}

const lx_host_api kDefaultHostApi = {
    sizeof(lx_host_api),
    LX_ABI_VERSION,
    &crt_alloc,
    &crt_free,
    &crt_log,
    &crt_state_intern,
    &crt_state_resolve,
};

}  // namespace

const lx_host_api& default_host_api() noexcept {
  return kDefaultHostApi;
}

}  // namespace lx
