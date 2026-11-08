# C23 library coverage audit

This is a Windows x86-64 implementation audit, not a conformance claim. The
reference is WG14 N3220; the N3221 editor's report says N3220 differs from
final DIS N3219 only by an Annex K footnote. See [N3220](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n3220.pdf)
and [N3221](https://open-std.org/jtc1/sc22/wg14/www/docs/n3221.htm). The
31-header API inventory is in [C23-API-AUDIT.md](C23-API-AUDIT.md). Required
names, declarations, and link results are separate from semantic evidence.

## Supported profile

Windows x86-64 is the only supported target. The six project-owned compiler
interface headers use predefined compiler macros, types, and builtins; the
standard headers do not include Clang resource headers. Strict C23 fixtures
use `-nostdinc` with project headers only.

Finite runtime contracts and their evidence are listed in
[C23-CONTRACT-AUDIT.md](C23-CONTRACT-AUDIT.md). The evidence focuses on
specified edge behavior and known implementation choices; it does not prove
every input or execution.

| Area | Profile evidence | Qualification |
| --- | --- | --- |
| Core scalar math (N3220 7.12) | C23 family, classification, rounding, exception, narrowing, and conversion matrices; see the math section of `C23-CONTRACT-AUDIT.md`. | `long double` is binary64 on this target. Numerical accuracy is implementation-defined within N3220 limits; no universal ULP guarantee is claimed. |
| Complex arithmetic (N3220 7.3) | Core function ranges, branch points, signed-zero manipulation, and large finite inputs have focused tests. | IEC 60559 Annex G behavior is conditional and is not claimed. |
| Formatted I/O (N3220 7.23) | Binary and width modifiers, rounding, output counts, UTF conversion, scan return/subject rules, stream state, and errors have focused tests. | Finite matrices do not enumerate every format and argument combination. |
| Allocation and utility (N3220 7.24) | Valid sized-free use, `memalignment`, explicit clearing, sorting/search, integer conversion, and guard-page boundaries have focused tests. | Invalid sized-free arguments violate preconditions. Annex K `_s` interfaces are not claimed. |
| Locale, character, and Unicode (N3220 7.4, 7.11, 7.30–7.31) | Supported locale category behavior, classification, UTF restart/error state, and wide I/O have focused tests. | Only `C`, `POSIX`, `C.UTF-8`, and `C.utf8` names are supported; Unicode locale data is limited to `C.UTF-8`. |
| Time (N3220 7.29) | Normalization, representability, DST ambiguity policy, standard formatting conversions, ISO week-year boundaries, clock bases, and resolution queries have focused tests. | Host timezone rules depend on Windows configuration; the selected gap/fold behavior and abbreviation-table limit are documented in [IMPLEMENTATION-DEFINED.md](IMPLEMENTATION-DEFINED.md). |
| Threads and atomics (N3220 7.17, 7.28) | Generic atomic fallback, synchronization, once, thread lifecycle, and TSS iteration have focused tests. | Windows loader-lock and forced-termination limits are in [STATUS.md](STATUS.md). |
| Signals and nonlocal transfer (N3220 7.13–7.14) | Supported signal delivery and `setjmp`/`longjmp` context cases have focused tests. | Computational signal-handler return and recursive `raise` cases with undefined behavior are excluded. |

## Optional and conditional features

| Feature | Status |
| --- | --- |
| Annex K bounds-checking interfaces | Optional; no `__STDC_LIB_EXT1__` claim or `_s` API coverage. |
| IEC 60559 Annex F binary floating point | Not claimed. Per-type `*_IS_IEC_60559` macros describe format characteristics and do not imply Annex F conformance. |
| IEC 60559 Annex G complex arithmetic | Not claimed. The presence of `<complex.h>` functions does not imply Annex G semantics. |
| IEC 60559 decimal floating point / Annex H | Not provided; no DFP types, APIs, or feature macro claim. |
| IEC payload and total-order math extensions | Audit-only optional branch; production does not define `__STDC_IEC_60559_BFP__`. |

## Current integration status

The current project-only full verification passed all 11 gates in release and
debug, with 133 test executables per configuration. It checked 31 headers, 45
resolved includes (all project headers), nine strict compile fixtures, and API
surfaces of 715 default and 730 audit-only optional IEC functions, with zero
missing definitions or conflicts. The 12 `next_*` suites passed 38 runner
cases per configuration. The source tree has 134 `tests/*.c` files including
the shared `next_test.c` helper. Counts and API inventory are not semantic
conformance evidence.

Passing the configured suite does not certify C23 conformance.
