# libc23 design notes

Freestanding C23 standard library for clang, no Windows SDK / Visual Studio / CRT.

## Layout
| path | content |
|---|---|
| `include/` | public C23 headers (only standard names; internals use `__` prefix) |
| `src/<module>/` | portable implementation, one dir per area (string, stdlib, stdio, math, complex, ctype, wchar, locale, time, threads, signal, setjmp, fenv, stdbit, unicode, errno, startup) |
| `src/internal/` | private headers (`libc.h`, `stdio_impl.h`) |
| `platform/pal.h` | **the only contract between libc and the OS** (see below) |
| `platform/windows/` | Windows PAL backend, process entry point, and import definitions |
| `arch/x86_64/` | x86-64 context and floating-environment support |
| `tests/` | one program per test file, `tests/test.h` harness, `python build.py test` |
| `tools/` | generators (Unicode tables) and the `c23cc` compile driver |

## Build
Supported build scope: Windows x86-64 only. Use
`python build.py [build|test|clean] --platform windows --arch x86_64 [--config release|debug]`
to produce `build/windows-x86_64-<config>/libc23.lib`.
Windows import libraries (kernel32, ...) are generated from
`platform/windows/*.def` with `llvm-dlltool`, so no SDK is required.

Compiler flags of note: `-std=c23 -nostdinc -ffreestanding -fno-builtin -fno-short-wchar`
(**wchar_t is always 32 bit, also on Windows**), `-ffp-contract=off`, and
`-frounding-math` for math/stdio/stdlib sources.

The supported full verification command is `python tools/verify_all.py`. It
runs release and debug regression suites, standalone header and compile-fixture
checks, API audits, and the `c23cc` tgmath regression. Windows test builds use
the direct executor by default; Ninja remains
available with `--executor ninja`. The verification summary and logs are
written under `build/verification/`.

Configured builds check the required freestanding compiler flags and restrict
header lookup to project `include/` files; standard headers do not depend on the
Clang resource directory. Windows test verification requires `llvm-readobj`
and scans executable imports to reject host CRT dependencies. The C23-required API manifest is compiled across its
listed families; this and API symbol audits check declarations and linkage,
not full semantic conformance.

## Windows x86-64 ABI choices
* errno values use the library's fixed POSIX-compatible assignments; `errno` is thread-local via `__errno_location()`.
* `time_t` = `long long`, `clock_t` = `long long`, `CLOCKS_PER_SEC` = 1000000.
* signals: SIGINT 2, SIGILL 4, SIGABRT 6, SIGFPE 8, SIGSEGV 11, SIGTERM 15.
* `wchar_t` 32 bit (Unicode scalar), `wint_t` = `unsigned int`, `WEOF` = `0xFFFFFFFF`.
* Clang's Windows format checker assumes a 16-bit `wint_t` and can reject valid
  `%lc` arguments. Its host-ABI format diagnostics are disabled by default;
  define `LIBC23_ENABLE_HOST_FORMAT_DIAGNOSTICS` before including `<stdio.h>`
  to opt in to those diagnostics and their host ABI assumptions.
* Native narrow strings (argv, paths, environment) are UTF-8 on Windows.
* locales: "C"/"POSIX" (bytes map to same-valued wide chars) and "C.UTF-8".
* No text-mode newline translation: text streams == binary streams.
* `math_errhandling == MATH_ERREXCEPT` (floating-point exceptions, errno not set by libm).
* `FLT_ROUNDS` is queried at runtime from the active floating-point environment.
* `long double` has the binary64 format and precision of `double` on this target.
* Decimal floating point (`_Decimal32/64/128`, optional Annex H) is not provided
  (clang does not implement the types).  `__STDC_IEC_60559_DFP__` is undefined.

The supported implementation-defined choices are recorded in
[`IMPLEMENTATION-DEFINED.md`](IMPLEMENTATION-DEFINED.md).

## Platform Abstraction Layer
`platform/pal.h` documents the runtime-to-Windows boundary. Threads, mutexes,
condition variables and `call_once` are built on a
single futex-style primitive (`pal_futex_wait/wake`).

## Conventions for contributors
* One `.c` per function group; `-ffunction-sections` + archive keep binaries small.
* Library code must not call printf-family or anything outside its own layer
  unless that dependency is intended; no OS headers outside `platform/`.
* Public headers: include guards `_LIBC23_<NAME>_H`, `extern "C"` guards,
  and the C23 `__STDC_VERSION_<NAME>_H__ 202311L` macros where the standard requires them.
* Internal symbols: `__libc_*`, hidden visibility (`LIBC_HIDDEN`).
