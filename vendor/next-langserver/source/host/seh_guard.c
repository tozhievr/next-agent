#include "host/seh_guard.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int lx_seh_call(void (*fn)(void*), void* ctx, uint32_t* out_code) {
  __try {
    fn(ctx);
    return 1;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    if (out_code) {
      *out_code = (uint32_t)GetExceptionCode();
    }
    return 0;
  }
}
#else
int lx_seh_call(void (*fn)(void*), void* ctx, uint32_t* out_code) {
  (void)out_code;
  fn(ctx);
  return 1;
}
#endif
