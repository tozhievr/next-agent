# NextAgent: implementation plan and progress

Date: 2026-11-06. Source specification: user-provided `aed-architecture.md`, revision **1.1**, sections 1–19.

## Product versioning

Current release: **0.11.0** (minor: opt-in JSON-RPC diagnostic enforcement). Apply SemVer to each completed change:
- **Patch** (`x.y.Z`): minor compatible bugfixes, small corrections.
- **Minor** (`x.Y.0`): moderate to large backward-compatible additions that preserve architecture, including milestones.
- **Major** (`X.0.0`): global architectural redesigns / breaking stable APIs.

For 0.x pre-1.0, document breaking changes even if using minor bumps. Keep `pyproject.toml`, `src/nextagent/__init__.py` and `CHANGELOG.md` synchronized, and add a version consistency test. Avoid unexplained bumps.

## M0: SQLite, ChangeSet, CLI — initial foundation (partial)

- [x] Project scaffolding, pyproject and entrypoint.
- [x] Immutable Snapshot with line offsets and BLAKE2b-16.
- [x] Edit / ChangeSet application, inversion, map_pos, newline stats.
- [x] SQLite WAL, foreign keys and durable document heads.
- [x] Single-revision atomic multi-file writes; stale `if_rev` detection.
- [x] Append-only inverse undo; revision history.
- [x] Explicit atomic file flush; working memory separated from disk.
- [x] Baseline hash tracking and preflight external-change detection on flush.
- [x] Cross-platform Actions workflow targeting Python 3.16.0a1 exactly.
- [x] Starter tests, README, changelog and plan.
- [x] Basic disk baseline hash checks; conflicts raise `disk_conflict` rather than overwriting observed changes.
- [ ] Full M4 three-way merge, watcher and cross-process file-locking (TOCTOU races remain possible).
- [ ] Redo / arbitrary revision undo, migrations and compressed snapshots.
- [ ] Async single-writer queue and dedicated SQLite thread.
- [x] Experimental line-delimited JSON-RPC 2.0 stdio dispatcher (open/view/edit/history/sync).
- [ ] Unix socket transport, schema validation and full protocol coverage.

## Planned specification milestones

- [~] **M1:** C23 tree-sitter parsing, top-level symbol outline and exact-name selectors implemented; scoped tagged types and field selection implemented; robust nested declarators, full C23 constructs and semantic resolution remain.
- [~] **M2:** UTF-8/16/32 position codec and Content-Length LSP framing implemented; experimental clangd lifecycle, formatting request and diagnostics collection implemented; diagnostic delta comparison implemented separately; opt-in Store pre-commit gate implemented; optional JSON-RPC enforcement implemented; cross-file validation and history undo validation remain.
- [ ] **M3:** rename, references, add_param, include and quickfix refactoring.
- [ ] **M3b:** brain L0/L1 static metrics, graph, seen, notes, conventions.
- [ ] **M4:** branches, multi-session merging, watcher and three-way disk merge.
- [ ] **M4b:** brain L2 embeddings, hybrid search, worker processes.
- [ ] **M5:** MCP tools, compact renderers, response token budget.
- [ ] **M5b:** hierarchical summaries, pack, retrieval evaluation.
- [ ] **M6:** adapters for C++, Python, Rust.

## Test ledger

- Locally prepared M0 files were tested with **Python 3.13.5**, `pytest`: **10 passed** prior to publishing the repo changes; final repository includes additional test for canonical-path duplication and version consistency.
- Coverage: codepoint offsets (emoji, CJK), inverse changes, overlap and bounds checks, multi-file commits, restart durability, stale versions, rollback on failed batch, undo, path traversal, explicit flush.
- GitHub CI configuration: Ubuntu, Windows and macOS, exact `3.16.0a1`; **status unverified until GitHub job completion**. No Python 3.16 alpha runtime in local environment.
- Ruff/build not executable locally because dependencies could not be downloaded. CI will assess them if Python setup succeeds.
- Missing: Hypothesis/property-based fuzzing, golden C23 cases, clangd integration, crash/WAL tests, model benchmarks and token metrics.

## Known limitations

NextAgent 0.5.0 is an intentionally incomplete foundation. Partial syntax-based symbol addressing; no LSP or diagnostics; basic preflight protection but no cross-process file lock or three-way merge; current operations use explicit offsets; no concurrency isolation; no complete M0 redo or history compaction. Mark progress only on observed results rather than on aspirational architecture.

## 0.11.0 milestone notes

- Stage all changed files before candidate diagnostics, not sequential per-file validation.
- Tests assert all overlays are staged and no SQLite commit occurs on rejection.
- Unchanged dependent translation units remain outside gate coverage.
- CI pending.

## 0.10.1 milestone notes

- Hardened JSON-RPC against clangd diagnostic timeout and disconnected LSP process.
- Added regression tests for recovery without committing rejected edits.
- CI pending.

## 0.10.0 milestone notes

- Added opt-in pre-commit diagnostic validation to Store.undo and JSON-RPC history undo.
- Added real clangd parse-regression test in Linux CI (Python 3.16.0a1).
- Cross-file diagnostics, concurrency and comprehensive LSP integration remain.
- CI pending.

## 0.9.2 milestone notes

- Corrected clangd client initialization failure identified by v0.8.0 CI.
- Serialized LSP writes; added optional real-clangd smoke test (NEXTAGENT_TEST_CLANGD=1).
- Added version consistency test and corrected literal newline escapes in docs.
- CI pending for this patch.

## 0.9.0 milestone notes
\n- Added `nextagent serve --clangd` to enforce pre-commit diagnostic checks for JSON-RPC edits and symbol replacement.\n- Mock validator injection tests rejection and success through JSON-RPC.\n- Live clangd integration tests, cross-file checks and guarded undo still pending.\n- CI pending.\n\n## 0.8.0 milestone notes\n\n- Store.edit accepts optional validator callback, before revision creation, inside the SQLite transaction.\n- DiagnosticGate tests baseline/candidate clangd overlays, rejects new diagnostics, requires versioned reports, closes LSP documents.\n- On rejection, SQLite transaction rolls back and disk remains untouched.\n- No live clangd end-to-end test or default JSON-RPC integration; pending CI.\n\n## 0.7.0 milestone notes\n\n- Added standalone diagnostic comparison, duplicate-aware severity thresholds and fail-closed revision matching.\n- Fixed incorrect UTF-8 CJK test expectation observed in GitHub CI (1 failed, 28 passed on previous run).\n- No automatic Store gate/rollback yet; never treat this as validated edits.\n- CI must be verified for this commit.\n\n## 0.6.0 milestone notes\n\n- Experimental clangd subprocess client, initialize/shutdown, open/change, diagnostics snapshots and formatting requests.\n- Unit tests with mock transport; no live clangd integration test yet.\n- Does not yet validate or apply edits through Store; no rollback/diagnostics delta.\n- CI pending.\n\n## 0.5.0 milestone notes\n\n- Added strict LSP Unicode position codec and binary Content-Length transport framing.\n- Added malformed-frame and Unicode regression tests.\n- Not yet a clangd client: no process management, initialize handshake, diagnostics or formatting.\n- CI pending for this version.\n\n## 0.4.0 milestone notes\n\n- Added nested tagged type/field navigation with explicit scope disambiguation.\n- Added JSON-RPC `replace_symbol` with required workspace revision check.\n- Still no clangd/LSP or validation of generated replacement code; use with caution.\n- CI status must be checked for this commit before merge.\n\n## 0.3.0 milestone notes\n\n- Added tree-sitter-c grammar and initial top-level outline/select API, Unicode codepoint offsets, ambiguity detection.\n- Added CLI and JSON-RPC exposure and regression tests.\n- Not a complete C23 semantic resolver; M1 remains in progress. Await exact Python 3.16.0a1 CI.\n\n## 0.2.0 milestone notes

- Product identity standardized as NextAgent, distribution and import package `nextagent`, CLI `nextagent`; removed old `aed` Python package/command.
- Added experimental JSON-RPC 2.0 line-delimited stdio dispatcher and integration tests; no LSP/MCP framing.
- Updated README and version test. Await CI on exact Python 3.16.0a1; do not claim green before observing.

## 0.1.1 patch notes

- Added persistent disk baseline and hash comparison before `sync` overwrites files.
- Added three disk-conflict regression cases: external modification, multi-file preflight, repeated successful sync.
- GitHub Actions CI validates Python 3.16.0a1; current run status must be checked independently.
