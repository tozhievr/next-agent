# Native integration verification and measurements

Measured on this Windows 11 machine and WSL2 Ubuntu 26.04. These are local regression
results, not a claim that every supported language feature is correct.

## What ran

- Windows CTest: 6/6 suites, including GUI, actual HTTP/tool/check loop and thirty-module validation.
- Linux CTest: 5/5 suites. Both platforms also pass bundled language integration and the 51-line parity transcript.
- All 30 modules: nonempty grammar-appropriate source, extension routing, clean syntax diagnostics, bounded codepoint symbol ranges, incremental whitespace edits, comparison with a fresh-process full parse, and fresh document/snapshot versions.
- C/C++/Python: Unicode comments and whole-symbol views. C/C++/JSON/Python/TypeScript: invalid syntax must produce errors, then recover on restoration.
- Strict TypeScript compiler checks passed for tasks 14–17 (`tsc --noEmit --strict --target ES2022 --module ESNext --moduleResolution bundler`).

The built-in C23 lexer/scanner files were deleted. All symbol extraction now goes
through next-langserver, including the snapshot compatibility adapter. C-only
refactoring helpers still make syntax-based edits; semantic binding is not claimed.
The Linux `plugins/libc.so` is the C language plugin, unrelated to the removed libc23 runtime.

## Thirty coding tasks

The assistant implemented all 30 supplied contracts through NextAgent transactions
before reading evaluator output. Instructions/interfaces were inspected; evaluator
test source was withheld during solution writing. No paid OpenRouter agent run was
performed, and these results are not an OpenRouter model success rate.

Solutions are in `tests/benchmark_solutions/`. Evaluation copies live under
`build/integration-evaluation/`. The original `tests/integration/` files were not
edited. Their content hashes are in `integration-inputs.sha256.json`.

- Original supplied fixtures with dependencies installed: **27/30** ([raw results](integration-original.json)).
- Explicit evaluator-only fixture repairs: **30/30** ([raw results](integration-repaired.json)).
- Solutions were unchanged between those evaluations.

Three original fixture defects explain the difference:

1. Task 02 embeds double-encoded Unicode literals while expecting original UTF-8 byte sequences. Evaluator copy uses explicit byte escapes, including intentionally invalid encodings.
2. Task 05 compiles a Kotlin top-level function declaration without a body alongside its implementation. Evaluator copy treats that interface line as a signature comment.
3. Task 30 expects the final `)` at offset 11 in `10 + 20*(3-1)`; its actual zero-based byte offset is 12.

The first attempt scored 23/30 because four additional TypeScript tasks lacked a
locally resolvable `tsx` dependency. Installing it fixed the environment without
changing solutions. HTML/CSS checks are static; GLSL checks compile and inspect
source, without GPU output comparisons. There are no private adversarial tests
or comparison runs against another agent/version, so this is not a validated
competitive benchmark.

Reproduce in WSL with g++, Kotlin/JDK, Swift, GHC, glslang-tools, Node, tsx and TypeScript:

```sh
python tools/evaluate_integration.py --prepare
# In WSL: npm install --prefix /mnt/d/next-agent/build/integration-evaluation tsx typescript
python tools/evaluate_integration.py --report docs/benchmarks/integration-original.json
python tools/evaluate_integration.py --repair-fixtures
python tools/evaluate_integration.py --report docs/benchmarks/integration-repaired.json
```

The evaluator runs trusted test commands as the current WSL user with time limits.
It is isolated by directory, not by an operating-system security sandbox.

## Measured RPC latency and response size

Corpus: 100 C++ functions, 4,980 UTF-8 bytes. One persistent RPC server, sequential
requests, 25 repetitions for each warm operation. Numbers below are median / p95
milliseconds, including Python JSON transport. The edit measurement includes a
transaction, flush and native incremental outline. Other operations have no writes.

| Operation | Windows ms | WSL2 ms |
|---|---:|---:|
| Full view | 0.191 / 0.315 | 4.922 / 5.181 |
| Symbol view | 0.439 / 0.630 | 7.738 / 8.361 |
| Warm outline | 0.911 / 1.496 | 8.042 / 8.913 |
| Edit + flush + outline | 33.324 / 38.215 | 74.825 / 81.578 |

Cold first outline: Windows **4.277 s**, WSL2 **4.133 s**.
Each cold value is one observation, not a latency distribution. This startup cost
remains a performance limitation. WSL reads the same repository on `/mnt/d` (Windows
filesystem); these figures must not be presented as a general Windows/Linux ranking.

A full view returned 5156 bytes of JSON; one-symbol view returned
141 bytes: **36.6x less response data** on this corpus. This is not
a measured token or API-cost saving. Cold setup, model quality and task-level tool
selection can outweigh the response-size reduction.

Raw measurements: [Windows](native-windows.json), [Linux](native-linux.json).
Reproduce with `python tools/benchmark_native.py` on each platform. Timings naturally
vary with machine load, filesystem and the existing journal revision envelope.

## Limits and native fixes

The module matrix covers regression examples, not whole language specifications.
Mermaid supports ER/class diagrams; its bundled grammar does not support flowcharts.
Native syntax diagnostics and references do not provide compiler-level semantic analysis.
Upstream target-language actions, contextual typedef/type binding and some lexer
predicates remain incomplete. See the [native override notes](native-overrides.md)
for the C, C#, Go, Swift and PHP repairs found during this run.

All these limitations are distinct from successful plugin loading. Detailed matrices:
[Windows](language-matrix.json), [Linux](language-matrix-linux.json).
