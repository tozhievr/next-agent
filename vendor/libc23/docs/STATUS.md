# Implementation status

This is a developing freestanding library, not a C23 conformance claim.
Windows x86-64 is the sole supported project target. Required header APIs are
inventoried in [C23-API-AUDIT.md](C23-API-AUDIT.md); finite semantic evidence
for the exercised profile is summarized in [C23-COVERAGE.md](C23-COVERAGE.md)
and [C23-COMPLETION.md](C23-COMPLETION.md).

## Implemented and exercised on Windows x86-64

- Narrow string/memory functions, integer conversions and utility algorithms.
- Allocator size classes, large mappings, aligned allocation/reallocation,
  `memalignment`, `free_sized`, and `free_aligned_sized`.
- `C` and `C.UTF-8` locales, character classification, and UTF conversions.
- UTC/calendar conversion, local time, narrow/wide time formatting, and clocks.
- Buffered narrow I/O; narrow and wide formatted output, scanning, and streams.
- Binary32/binary64 parsing and binary64 decimal/hex formatting with directed
  rounding; C23 `strfrom*`, `%b`/`%B`, `wN`/`wfN`, and const-preserving searches.
- Callable and generic C atomics; C threads, synchronization, once, and TSS.
- C23 scalar math families, including exponential/logarithmic and pi-scaled
  functions, power families, min/max, `fromfp`, narrowing arithmetic, and
  classification/canonicalization operations.
- Complex arithmetic and `tgmath.h` dispatch, with focused branch-cut tests.
- Windows startup, argument/environment conversion, signals, and PAL services.
- Runtime `FLT_ROUNDS`, architecture floating-environment support, compiler
  `__int128` division and floating conversions, and the Windows
  performance-counter clock path.
- Standard I/O `perror` state preservation, unique `tmpnam` generation, and a
  bounded printf NaN output length exposed as `_PRINTF_NAN_LEN_MAX`.

## Target and verification limits

- `long double` has binary64 format and precision on Windows x86-64.
- Numerical accuracy has focused regression coverage, but no universal ULP
  guarantee is claimed for every function. C permits implementation-defined
  accuracy choices under N3220 5.2.5.3.3; selected vectors do not prove full
  conformance.
- Supported implementation-defined behavior is recorded in
  [IMPLEMENTATION-DEFINED.md](IMPLEMENTATION-DEFINED.md).
- TSS destructor callbacks on native thread return and `ExitThread` run under
  the loader lock; user destructors must avoid loader-dependent work. Forced
  thread termination is not covered, and process detach does not call TSS
  destructors.
- DST gap/fold behavior is tested against a synthetic timezone; host timezone
  rule coverage depends on Windows configuration. A saturated zone abbreviation
  table yields unknown `%z`/`%Z` metadata rather than mislabeling a zone.
- Passing the configured tests is not C23 conformance certification.

## Verification

The current project-only `python tools/verify_all.py` run passed all 11 gates in
release and debug: 133 test executables per configuration. It checked 31
headers, 45 resolved includes (all project headers), nine strict compile
fixtures, and API surfaces of 715 default functions and 730 functions in the
audit-only optional IEC branch, with zero missing definitions or conflicts.
There are 134 `tests/*.c` files including the `next_test.c` helper, producing
133 executables. The 12 `next_*` suites passed 38 runner cases per
configuration. The production build does not advertise
`__STDC_IEC_60559_BFP__`; the optional IEC branch is audit-only. Compilation,
header checks, and symbol counts are separate from semantic evidence.

Additional evidence includes 451 active floating-mode/exception checks, 1,638
signed/unsigned 128-bit division checks, 7,208 float128 conversion checks,
174 performance-counter vectors, and the finite family matrices in
[C23-COMPLETION.md](C23-COMPLETION.md). `perror_state` covers 11 checks,
`tmpnam_unique` covers 261, and `_PRINTF_NAN_LEN_MAX` has a compile-time check.
The `max_align_t` alignment fix was checked with rebuilt release/debug
`malloc_core` runs (434 checks each), plus rebuilt `fundamental_headers` and
`printf_float` (48 checks). These focused results are not a blanket C23
conformance claim.

Configured builds enforce `-std=c23`, `-nostdinc`, `-ffreestanding`, and
`-fno-builtin`; standard-header lookup uses project headers without a Clang
resource include directory. The direct Windows runner uses `llvm-readobj` to
reject host CRT imports. The library has no UCRT/MSVCRT dependency; UCRT may be
loaded as a comparison oracle. Full logs are written under
`build/verification/`.

Run the full Windows x86-64 verification pipeline with:

```powershell
python tools/verify_all.py
```
