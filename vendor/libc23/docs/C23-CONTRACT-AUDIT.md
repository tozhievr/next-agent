# C23 contract audit: Windows x86-64

This is a finite checklist of mandatory C23 library contracts exercised for
the supported Windows x86-64 profile. The clause baseline is WG14 N3220,
which N3221 reports has no technical-content differences from final DIS N3219
apart from an Annex K footnote. The checklist records focused evidence; it is
not a conformance certification or proof of every possible execution.

References: [WG14 N3220](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n3220.pdf),
[WG14 N3221](https://open-std.org/jtc1/sc22/wg14/www/docs/n3221.htm).

## Focused required-contract evidence

| N3220 area | Contract exercised | Evidence and boundary |
| --- | --- | --- |
| 7.3, complex arithmetic | Principal result ranges, signed-zero-sensitive manipulation, projection, inverse-function branch behavior and large finite inputs. | `complex_core.c`, `complex_branchpoint.c`, `complex_extreme.c`, `complex_fenv.c`, and `next_complex_contract.c`. The tests deliberately do not assert conditional Annex G signed-zero/exception semantics. |
| 7.12, scalar math | C23 scalar families, special values, signed zero, ranges, rounding modes, floating exceptions, classification, conversion and narrowing behavior. | `math_c23_core_matrix.c`, `math_classify_matrix.c`, `math_classify_flags.c`, `math_float_matrix.c`, `math_real_family_edges.c`, `math_power_c23.c`, `math_exp_c23.c`, `math_pi_edges.c`, `math_fromfp.c`, `math_narrow_reference.c`, `math_exceptions.c`, and `next_math_contract.c`. Binary64 `long double` is the Windows x86-64 target format. The implementation does not promise a universal ULP bound. |
| 7.13, nonlocal transfer | `setjmp`/`longjmp` context behavior and floating-environment interaction. | `setjmp_core.c`, `threads_fenv.c`, and `fenv_core.c`. The supported ABI is Windows x86-64 only. |
| 7.14, signal handling | Supported signal set, handler persistence, software delivery, ignore/default behavior, and invalid-signal handling. | `signal_core.c` and `next_threads_contract.c`. Returning from computationally generated `SIGILL`/`SIGFPE`/`SIGSEGV` handlers and `raise` from a handler entered by `raise` are excluded as undefined cases under 7.14.1.1p3–p4. |
| 7.17, atomics; 7.28, threads | Atomic flags and generic runtime fallback; thread lifecycle, synchronization, once, and TSS destructor iteration behavior. | `atomic_core.c`, `atomic_generic.c`, `atomic_runtime.c`, `threads_core.c`, `threads_timedwait.c`, `threads_stress.c`, `threads_exit.c`, and `next_threads_contract.c`. Native thread-detach and loader-lock limits are recorded in [STATUS.md](STATUS.md). |
| 7.23, formatted I/O | C23 binary and width modifiers, rounding-sensitive formatting, output bounds, scanner subject sequences, stream errors and close behavior. | `printf_float.c`, `format_c23_matrix.c`, `scanf_c23_matrix.c`, `scanf_subject_sequences.c`, `stdio_errors.c`, `stdio_close_standard.c`, `next_stdio_contract.c`, and `next_scan_contract.c`. UCRT comparisons are test oracles, not runtime dependencies. |
| 7.23, formatting and conversion details | Integer promotion for `wN`/`wfN`, precision/count/truncation, UTF-8 wide/narrow conversion, and encoding errors. | `next_format_contract.c` checks narrow/wide output. `next_stdio_contract.c` covers stream position and update transitions. `next_scan_contract.c` covers EOF/matching failure, `%n`, scansets, leading whitespace and multibyte character counts; its new boundary expectations are part of current integration verification. |
| 7.24, allocation and conversions | Valid sized deallocation pairs, alignment queries, guard-page accesses, integer conversion and `strfrom*` paths. | `malloc_core.c`, `string_guard.c`, `stdlib_numeric.c`, `stdlib_numeric_wide.c`, `bitint_codegen.c`, and compile fixtures. Invalid sized-free arguments violate preconditions and are not runtime cases. |
| 7.24, general utility edge contracts | Zero-element sorting/search, search qualification, aligned allocation and explicit clearing, random range/reproducible seeding. | `next_stdlib_contract.c`, `string_qual.c`, `stdlib_qual.c`, and `stdlib_numeric.c`. |
| 7.11 and 7.4, locale and character classification | Supported locale category/query behavior, compound restore/error transactionality, environment precedence, localeconv, `EOF`/`WEOF`, classification relationships and descriptors. | `next_locale_contract.c`, `ctype_narrow.c`, `ctype_wide.c`, and `locale_ctype_edges.c`. Supported locale names are finite and listed in [IMPLEMENTATION-DEFINED.md](IMPLEMENTATION-DEFINED.md). |
| 7.17, atomic query | `atomic_is_lock_free` result type, pointer evaluation exactly once, and query behavior without dereferencing its pointer. | `next_atomic_contract.c` with `atomic_generic.c` and `atomic_runtime.c`. Runtime locking/representation details remain target-specific implementation internals. |
| 7.26, generic search; 7.29, time | Const-preserving search results, calendar normalization and representability, DST choices, ISO week-year formatting, standard E/O conversions, supported time bases and resolution queries. | `string_qual.c`, `stdlib_qual.c`, `time_gm.c`, `time_dst.c`, `time_edges.c`, `time_clock.c`, and `next_time_contract.c`. Windows timezone rules are machine-dependent; gap/fold policy is documented in [IMPLEMENTATION-DEFINED.md](IMPLEMENTATION-DEFINED.md). `%Ob`/`%OB` are covered in the C locale. |
| 7.30–7.31, Unicode and wide characters | Restartable UTF conversion state, malformed/incomplete input, wide formatting/scanning, and locale interactions. | `multibyte_transitions.c`, `multibyte_restart.c`, `uchar_state.c`, `wide_matrix.c`, `wide_locale.c`, `scanf_wide.c`, `wscanf_core.c`, and `next_utf_contract.c`. The new contract includes zero-length `wcsrtombs` sizing behavior. Locale data is limited to `C` and `C.UTF-8`. |
| Header and link contracts | Required declarations, project-only headers, API definitions, and strict C23 compile surfaces. | The N3220 required-name fixture, standalone header checks, strict compile fixtures, and API audit are independent of runtime evidence. Current integration status belongs in [STATUS.md](STATUS.md). |

## Current wave changes and integration checks

The new contract suites make additional clauses and edge cases explicit. This
wave also fixed these concrete behaviors:

- Corrected the `wN` formatted-output argument cast and bounded the printable
  NaN representation with `_PRINTF_NAN_LEN_MAX`.
- Fixed `wcsrtombs` when the destination fills exactly at the available byte
  limit, and kept input-only streams unchanged by `fflush(NULL)`.
- Preserved negative zero in `expm1(-0.0)` and corrected the finite large-input
  branch in `casinh`.
- Accepted the required `%Ob`/`%OB` `strftime` conversions in the C locale.
- Made `atomic_is_lock_free` evaluate its argument once.
- Corrected scanner byte-item handling, EOF/assignment results, leading-space
  behavior for `%ls`, and whitespace skipping before literal `%%`.

The suites exercise and protect those changes:

- `next_complex_contract.c` checks core complex result ranges and selected
  branch points without asserting Annex G-specific behavior.
- `next_format_contract.c` covers `wN`/`wfN` promotions, precision/count
  behavior, UTF output, and encoding errors. The printf implementation now has
  an explicit `_PRINTF_NAN_LEN_MAX` bound; `printf_float.c` checks it.
- `next_stdio_contract.c` covers stream position/update transitions and
  injected read/write/close failures. `perror_state.c` exercises `perror`
  state preservation; `tmpnam_unique.c` checks generated-name uniqueness.
- `next_locale_contract.c` covers category updates, compound-locale restore,
  environment precedence, localeconv, ctype, and wide descriptors. Environment
  values use the exact supported locale-name list; arbitrary codeset suffixes
  such as `en_US.UTF-8` are rejected.
- `next_atomic_contract.c` checks that `atomic_is_lock_free` evaluates its
  pointer argument once and exposes the required boolean result type.
- `next_time_contract.c` checks valid `timegm` result `-1`, normalization,
  range-failure state, timezone-independent `mktime` round-trip, `%Ob`/`%OB`,
  ISO week-year boundaries, and supported `timespec_get`/`timespec_getres`
  bases.
- `bitint_codegen.c` exercises compiler-provided `__int128` conversions and
  runtime support; this remains a Windows x86-64 compiler/runtime integration
  check.

The scanner expectations for literal `%%` whitespace and narrow `%lc`
byte-width behavior were corrected and the targeted scanner suites pass.
The integrated project-only release/debug verification passed all 11 gates,
with 133 test executables and 38 `next_*` runner cases per configuration. The
suite contains 134 `tests/*.c` files including the shared helper. The verification
also passed 31 headers, 45 project-only resolved includes, nine compile
fixtures, and the 715/730 API audit with zero missing definitions or conflicts.
These counts do not imply blanket ISO conformance.

## Time sub-audit

The time functions were checked against N3220 7.29.2.3–.7 and 7.29.3.3–.5.
`timegm` and `mktime` accept unrestricted calendar-field inputs (except the
ignored `tm_wday` and `tm_yday` values), normalize on success, and return
`(time_t)-1` when their result or normalized `tm_year` cannot be represented.
The failure contract preserves `tm_wday`; errno behavior is an implementation
choice documented separately. Tests include the valid instant `(time_t)-1`.

`timespec_get` returns the requested base on success and zero for unsupported
bases or clock errors. `timespec_getres` reports each defined base, permits a
null output pointer, and returns zero for an unsupported base. Returned
resolution values are stable for repeated calls within a program invocation.
The supported Windows bases are `TIME_UTC`, `TIME_MONOTONIC`, `TIME_ACTIVE`,
and `TIME_THREAD_ACTIVE`.

`strftime` and `wcsftime` cover the required conversion set and E/O modifiers
for the supported C locale. ISO week-year boundaries are checked. `%z` and
`%Z` produce no characters when time-zone information is indeterminate; libc
producer metadata is retained by ordinary `struct tm` copies. Nonstandard
conversions such as `%s` are outside the required C23 contract.

## Scope and limits

Only the Windows x86-64 configuration is a supported target. The profile does
not claim Annex K, IEC 60559 Annex F/G/H, decimal floating-point, or universal
floating-point accuracy. Locale names and timezone rules are implementation
choices described in [IMPLEMENTATION-DEFINED.md](IMPLEMENTATION-DEFINED.md).
Focused passing tests do not establish blanket ISO conformance.
