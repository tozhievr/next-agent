# Windows x86-64 C23 semantic verification

This checklist records finite runtime evidence for the Windows x86-64 profile,
using WG14 N3220 as the C23 text baseline. WG14's N3221 editor report states
that N3220 has no technical-content differences from final DIS N3219; the only
noted difference is an Annex K footnote. These matrices are evidence for the
exercised profile, not proof of every possible execution.

References: [WG14 N3220](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n3220.pdf),
[WG14 N3221](https://open-std.org/jtc1/sc22/wg14/www/docs/n3221.htm).

Annex K, decimal floating point, and IEC 60559 Annex F/G/H behavior are outside
the claimed profile. `long double` uses binary64 on this target.

## Evidence matrices

| Area | Evidence recorded | Boundary |
| --- | --- | --- |
| Required names and headers | N3220 required-name fixture, 31 standalone header checks, 45 resolved project-only includes, nine strict compile fixtures, and the API audit passed with zero missing definitions or conflicts. | The 730-function optional IEC branch is audit-only. Declarations and linkage are separate from semantic evidence. |
| Search qualification and threads | `string_qual.c` and `stdlib_qual.c` check const/mutable generic results and single evaluation; `threads_stress.c` checks repeated TSS destructor iterations. | Windows thread detach limitations are in [STATUS.md](STATUS.md). |
| Time | `time_gm.c`, `time_dst.c`, `time_edges.c`, and `time_clock.c` cover normalization/range boundaries, synthetic DST transitions, standard narrow/wide format directives and modifiers, and clock conversion/error cases. | Host timezone rules remain machine-dependent. A full abbreviation table yields unknown `%z`/`%Z` metadata, documented in [IMPLEMENTATION-DEFINED.md](IMPLEMENTATION-DEFINED.md). |
| Math | `math_c23_core_matrix` (970 cases), classifier matrices (54 + 60), float matrix (73), real-family edges (198), and `pow10` cases (184); narrowing arithmetic (2,404 checks), decimal-function vectors (830), and float128 conversion checks (7,208). | `long double` is binary64. IEC payload/total-order and Annex F/G semantics are outside scope. No universal ULP guarantee is claimed. |
| Formatted I/O | `printf_reference` compares 550,398 nearest-mode outputs with UCRT; `printf_exact` checks 3,221 exact cases across four rounding modes. Output matrix: 336 cases. Scanner matrix: 284 cases; subject sequences: 87 cases. `perror_state` (11 checks), `tmpnam_unique` (261 checks), and `_PRINTF_NAN_LEN_MAX` have focused regression/compile coverage. | Finite cases do not establish every format and argument combination. UCRT is a comparison oracle only. |
| Allocation | `malloc_core` covers valid sized frees; `memalignment` covers ordinary and aligned allocations plus interior pointers; `string_guard` checks 21 guard-page boundaries. | Invalid sized-free arguments violate the interface preconditions. |
| UTF conversion and wide I/O | `multibyte_transitions`, `wide_matrix` (91), `uchar_state` (84), and `multibyte_restart` (33). | Locale support is limited to `C` and `C.UTF-8`. |
| Target facts and implementation choices | `fundamental_headers.c` checks binary format macros; `FLT_ROUNDS` is queried from the active environment. [IMPLEMENTATION-DEFINED.md](IMPLEMENTATION-DEFINED.md) records supported choices. | Format macros do not imply Annex F support. |

The full project-only verification passed all 11 release/debug gates with 133
test executables per configuration; the 12 `next_*` suites passed 38 runner
cases in each configuration. The source tree has 134 `tests/*.c` files,
including the shared helper. The `max_align_t` scalar `__int128` alignment fix
also passed rebuilt release/debug `malloc_core` checks, plus rebuilt
`fundamental_headers` and `printf_float`. Successful compilation, linkage, and
API counts are not semantic evidence. Optional facilities and targets outside
Windows x86-64 are not covered by this profile.
