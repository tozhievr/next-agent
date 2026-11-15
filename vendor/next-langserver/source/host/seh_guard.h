#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Invokes `fn(ctx)` inside a Windows SEH __try/__except block.
 * Returns 1 if the call completed without SEH exception, 0 if an SEH exception
 * was caught (storing the exception code in *out_code when non-NULL).
 */
int lx_seh_call(void (*fn)(void*), void* ctx, uint32_t* out_code);

#ifdef __cplusplus
}

#include <exception>
#include <string>
#include <type_traits>

namespace lx::host {

struct GuardResult {
  bool ok{true};
  uint32_t seh_code{0};
  std::string cpp_error;
};

template <typename Fn>
GuardResult guarded_call(Fn&& fn) {
  struct State {
    Fn& fn_ref;
    bool cpp_threw{false};
    std::string cpp_msg;
  } state{fn, false, {}};

  auto trampoline = +[](void* raw) {
    auto* s = static_cast<State*>(raw);
    try {
      s->fn_ref();
    } catch (const std::exception& e) {
      s->cpp_threw = true;
      s->cpp_msg = e.what();
    } catch (...) {
      s->cpp_threw = true;
      s->cpp_msg = "unknown C++ exception";
    }
  };

  uint32_t code = 0;
  int seh_ok = lx_seh_call(trampoline, &state, &code);
  GuardResult res;
  if (!seh_ok) {
    res.ok = false;
    res.seh_code = code;
  } else if (state.cpp_threw) {
    res.ok = false;
    res.cpp_error = std::move(state.cpp_msg);
  }
  return res;
}

}  // namespace lx::host
#endif
