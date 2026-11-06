# NextAgent: implementation plan and progress

Date: 2026-11-06. Source specification: user-provided `aed-architecture.md`, revision **1.1**, sections 1–19.

## Product versioning

Current release: **0.3.0** (minor: initial tree-sitter C23 navigation). Apply SemVer to each completed change:
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

- [~] **M1:** C23 tree-sitter parsing, top-level symbol outline and exact-name selectors implemented; robust symbol regions, nested symbols, full C23 constructs and semantic resolution remain.
- [ ] **M2:** clangd LSP client, UTF-8/16/32 position codec, formatting, diagnostics delta, rollback.
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

NextAgent 0.2.0 is an intentionally incomplete foundation. Partial syntax-based symbol addressing; no LSP or diagnostics; basic preflight protection but no cross-process file lock or three-way merge; current operations use explicit offsets; no concurrency isolation; no complete M0 redo or history compaction. Mark progress only on observed results rather than on aspirational architecture.

## 0.3.0 milestone notes\n\n- Added tree-sitter-c grammar and initial top-level outline/select API, Unicode codepoint offsets, ambiguity detection.\n- Added CLI and JSON-RPC exposure and regression tests.\n- Not a complete C23 semantic resolver; M1 remains in progress. Await exact Python 3.16.0a1 CI.\n\n## 0.2.0 milestone notes

- Product identity standardized as NextAgent, distribution and import package `nextagent`, CLI `nextagent`; removed old `aed` Python package/command.
- Added experimental JSON-RPC 2.0 line-delimited stdio dispatcher and integration tests; no LSP/MCP framing.
- Updated README and version test. Await CI on exact Python 3.16.0a1; do not claim green before observing.

## 0.1.1 patch notes

- Added persistent disk baseline and hash comparison before `sync` overwrites files.
- Added three disk-conflict regression cases: external modification, multi-file preflight, repeated successful sync.
- GitHub Actions CI validates Python 3.16.0a1; current run status must be checked independently.
