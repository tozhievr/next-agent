# Changelog

All product versions follow [Semantic Versioning](https://semver.org/).

## 0.33.0 - 2026-11-09

### Added
- `replace PATH --lines A-B (--new S | --new-file F)` (RPC `replace_text` with `start_line`/`end_line`): replaces whole lines without quoting the old text. Line endings of the file are kept, the final newline is supplied, an empty replacement deletes the lines.

## 0.32.0 - 2026-11-09


### Added
- External embedding model for `locate` / `hybrid_search`, configured in `.aed/embed.json`, with our own small caller and no heavy backend: `{"url": "http://127.0.0.1:11434/v1/embeddings", "model": "..."}` (OpenAI-style or Ollama `/api/embed`, plain HTTP over a built-in TCP client: Winsock on Windows, raw syscalls on Linux, no DNS, no TLS) or `{"command": "exe", "args": "..."}` (JSON lines on stdin/stdout). Optional `api_key_env`, `batch`, `timeout_ms`, `max_chars`.
- The model becomes a fourth ranking signal (kNN, cosine). Vectors are normalised and cached in `.aed/embed.cache` keyed by model and text, so each chunk is sent once; `locate --embed N` / `embed_budget` limits how many new chunks one query embeds (default 256, best candidates first), the result reports `embedding.pending`. A model whose vector size differs from the cache resets it. Backend errors never fail a search; they are reported in `embedding.error`.
- Platform layer: `pf_tcp_connect/pf_sock_*` (IPv4 literal or `localhost`).

## 0.31.0 - 2026-11-09


### Added
- `locate` / `hybrid_search` now has a third signal: kNN over local embeddings. "Random indexing" learns from the workspace itself which words occur in the same code (no model, no network, deterministic), chunk and query vectors are weighted sums of word vectors, similarity is the cosine; fused with BM25 over code words and BM25 over symbol/path names by weighted reciprocal rank fusion.
- Persistent search cache (kept with the workspace index in `serve`/`mcp`): chunks are cached per file by content hash, so a query after an edit re-tokenizes only the changed files.

### Changed
- Ranking: doc comments belong to the symbol below them, light stemming (`flushing` ~ `flush`), one-liners and non-symbol chunks are down-weighted, exact symbol name and exact phrase get a bonus.

## 0.30.0 - 2026-11-08


### Added
- Hybrid search: `locate QUERY` (RPC/MCP `hybrid_search`) ranks functions and code chunks for a natural-language or identifier query. BM25 over code words (camelCase and snake_case are split, so `drawButton` finds `draw_button`), BM25 over symbol and path names and an exact-phrase signal are fused with reciprocal rank fusion. Deterministic, no libm, identical on Windows and Linux. No embeddings or model needed.

## 0.29.0 - 2026-11-08


### Added
- Agent ergonomics (fewer calls, fewer tokens): `search` (grep over the index with context), `patch` (many literal replacements across files as one revision), `view --symbol NAME`, `--brief` compact output (`NEXTAGENT_BRIEF=1`).
- Every CLI command that changes text now writes it to disk right away (`--no-sync` to keep it in the journal only); before, forgetting `sync` left the disk behind the journal.
- `symbol_not_found` lists similar names; `ambiguous_symbol` lists the candidates with kind and line.

### Changed
- CHANGELOG.md is UTF-8 (it contained a stray Windows-1252 byte NextAgent itself rejected).

## 0.28.0 - 2026-11-08

### Added
- `add_param` through the language server: the server resolves which occurrences are the function (declarations, definitions, calls); only those are edited, so same-named functions elsewhere are left alone. Without a server the token-based version still works (`source`: `syntax`).
- CLI/RPC editing commands for working on a workspace with NextAgent itself: `replace PATH --old/--new` (literal text, unique match unless `--all`; LF text is converted to CRLF in CRLF files), `replace-symbol PATH NAME --new/--new-file`, `view PATH --lines A-B --raw`; RPC `replace_text` and `view` with `start_line`/`end_line`.
- `serve --lsp --no-gate`: language servers for navigation and refactoring without the diagnostic gate; `NA_LSP_TRACE=1` prints every LSP message (debugging).
- Cross-file queries wait until the server has analysed the synced documents (`lsp_client_settle`).

### Changed
- The diagnostic gate sends header changes before source changes. URIs use a lowercase drive letter on Windows.

## 0.27.0 - 2026-11-08

### Changed
- Language servers first for every language, C included: `outline`, `select` and `replace_symbol` ask the server (clangd for C) and fall back to the built-in scanner only when no server is installed or the request fails.
- A declaration and its definition (or several definitions the server lists among each other's references) resolve to one symbol instead of `ambiguous_symbol`.

## 0.26.0 - 2026-11-08

### Added
- Workspace index (`src/index.c`): symbols of every file from the language servers (C-family files fall back to the built-in scanner) plus an identifier inverted index; files are re-extracted only when their content changed.
- Semantic `references` and `rename` for any language with a language server: the symbol is resolved to one definition through the index (`kind`, `scope`, `path` disambiguate; otherwise `ambiguous_symbol`), the server answers `textDocument/references` / `rename`, and the WorkspaceEdit is applied as one atomic revision through the diagnostic gate. Servers without rename support fall back to replacing every reference.
- `find_symbols` searches every indexed language and includes members.

### Changed
- Without a language server the C token-based `references` / `rename` still work (result field `source`: `syntax`); with one, `source` is `lsp`.

## 0.25.0 - 2026-11-08

### Added
- Linux x86-64 support: raw-syscall platform layer (`src/platform_posix.c`), no glibc/musl needed.
- `build.py`: builds with clang + libc23 (vendored in `vendor/libc23`) for Windows or Linux; `test` runs the suite.
- Cross-platform parity check (`tests/parity.py`, `tests/parity.golden`): identical output on Windows and Linux.

## 0.24.0 — 2026-11-08

### Added
- Language server hub (`serve --lsp`, `mcp --lsp`): other languages through any LSP server — clangd, pyright/pylsp, rust-analyzer, gopls, typescript-language-server, zls, lua-language-server — chosen by file extension, started on demand, configurable in `.aed/lsp.json`.
- `outline`, `select` and `replace_symbol` for non-C files via `textDocument/documentSymbol`; the diagnostic gate validates edits to those files too.
- Incremental document sync: edits are sent as ranged `didChange` (common prefix/suffix trimmed) when the server advertises incremental sync.
- `lsp_servers` method and MCP tool.
- Tests run against a fake language server (`tests --fake-lsp`), including ranged-sync round trips.

### Changed
- `ClangdClient` is now the generic `LspClient` (old names remain as aliases); documents are tracked per client, reopening a document starts a fresh version space.

## 0.23.0 — 2026-11-08

### Added
- Incremental C23 outline: a per-document cache finds the changed byte range, re-lexes only a small window around it and splices the result with the unchanged top-level items (including nested symbols). JSON-RPC/MCP servers keep up to 32 caches; a one-character edit in a 40 KB file lexes about 11% of the tokens of a full scan. A differential fuzz test (300 random edits) checks that the cache is identical to a from-scratch scan.

### Changed
- A function whose body is never closed is now handled like the unterminated tail of the file, so results no longer depend on how much text follows it.

## 0.22.0 â€” 2026-11-08

### Changed
- Journal commits no longer store the full document text on every revision: a full text is written only as a checkpoint (first revision of a document, or once accumulated deltas outweigh the text); other revisions carry only forward/inverse edits. A 56 KB file edited 60 times now journals about 1x its size instead of about 60x. Old journals remain readable; journals written by 0.22 are not readable by older versions.

## 0.21.0 â€” 2026-11-08

### Added
- M3 refactoring (syntactic, comment/string/`#include`-aware): `references`, `rename`, `add_param`, `add_include`, `find_symbols` as CLI commands, JSON-RPC methods and MCP tools. Multi-file edits commit as one revision and see unflushed documents.

### Fixed
- Test sources no longer depend on the compiler's encoding of non-ASCII string literals.

## 0.20.1 â€” 2026-11-06

### Added
- Build with clang: `./build.ps1 -Compiler clang` (or `NA_COMPILER=clang ./build.sh`); stdio is switched to binary mode under the MSVC CRT.
- `install.ps1` one-liner installer (downloads the latest release, verifies SHA-256, updates the user PATH).
- Release workflow: pushing a `v*` tag builds with clang, runs the tests and publishes `nextagent-<version>-windows-x64.zip` plus `SHA256SUMS.txt`.
- `docs/AGENT_GUIDE.md`, a detailed usage guide for AI models.

### Changed
- CI now builds and tests with clang (always) and with nextc (optional, when `NEXTC_REPO` is set).

## 0.20.0 â€” 2026-11-06

### Changed (breaking)
- Complete rewrite in C23, built with the nextc compiler; no Python, SQLite or tree-sitter dependency.
- State is an append-only checksummed journal `.aed/journal.aedj`. The old `.aed/state.sqlite3` is not read or migrated.
- CRLF is preserved exactly.
- Default `--clangd` severity threshold is warnings and above (as before); errors are always blocking.

### Added
- `redo`, `undo --rev N` (rebased through later revisions, `undo_conflict` on overlap).
- Cross-process write lock and torn-tail recovery.
- MCP stdio bridge (`nextagent mcp`).
- JSON-RPC parameter type validation; `history` accepts `op=redo` and `rev`.
- Native test suite (`build.ps1 test`) including live clangd tests when clangd is installed.

### Removed
- Python package, `pyproject.toml`, pytest suite and Python-based CI.

### Not implemented
- Branches, offline brain, M3 refactorings, three-way merge/watcher, async writer, socket transport, M5b.

### Limitations
- CI workflow rewritten but not yet run.
- clangd does not re-diagnose unchanged dependents for semantic changes to an overlay header.

## 0.19.1 â€” 2026-11-06

### Fixed
- Reject virtual document inventories without a snapshot reader, rather than attempting to read nonexistent disk files.
- Use a set for virtual path membership during bounded dependency traversal.
- Added guard and file-limit tests.

## 0.19.0 â€” 2026-11-06

### Added
- Public read-only Store.snapshot_paths() enumerates tracked document heads, including unflushed new files.
- Dependency scanner accepts snapshot_paths and traverses Store-only source and header files.
- DiagnosticGate and JSON-RPC pass Store document inventory into dependency discovery.
- Added direct and transitive tests for Store-only translation units.

### Limitations
- clangd may not resolve Store-only headers from dependent translation units without filesystem materialization; diagnostic gate is not a complete virtual filesystem.

## 0.18.4 â€” 2026-11-06

### Fixed
- Dependency traversal now recognizes changed unflushed headers in the Store even if they do not yet exist on disk.
- Added direct and transitive dependency regression tests for new unflushed headers.
- Verified live clangd header regression test passed on Ubuntu CI for prior commit.

### Limitations
- Unflushed source files are still not enumerated by the dependency scanner.

## 0.18.3 â€” 2026-11-06

### Fixed
- DiagnosticGate captures bounded C/C++ filesystem path inventory and compile_commands.json bytes during validation.
- Store.edit rechecks build state inside the write transaction; rejects changes as stale_build_state.
- Tests cover changed compilation database, added translation unit and stable state.

### Limitations
- Build-state check does not freeze external filesystem; paths can change after recheck and before commit.
- Only C/C++ source/header path inventory and two supported compilation database locations are tracked.

## 0.18.2 â€” 2026-11-06

### Fixed
- Updated transaction-boundary regression test for speculative validation outside SQLite BEGIN IMMEDIATE, as intended since v0.18.0.

## 0.18.1 â€” 2026-11-06

### Fixed
- DiagnosticGate records the dependent document texts read during validation.
- Store.edit rechecks the validator read-set after acquiring the SQLite write lock, rejecting stale dependencies before commit.
- Added regression tests for dependency mutation during validation and unchanged dependencies.

### Limitations
- Filesystem discovery and compile_commands.json may still change during validation without detection; read-set covers document contents only.

## 0.18.0 â€” 2026-11-06

### Changed
- Store.edit performs optional diagnostic validation before BEGIN IMMEDIATE, reducing SQLite writer lock duration during clangd waits.
- Rechecks workspace revision and candidate baseline text inside the write transaction before committing.
- Added tests for unlocked validation, concurrent revisions, disk edits and rejected validators.

### Limitations
- Optimistic preflight may need retry on stale revisions; dependency files are not individually rechecked after validation.

## 0.17.0 â€” 2026-11-06

### Added
- Public read-only Store.snapshot_text(name) accessor that does not import documents, write to SQLite or commit active transactions.
- JSON-RPC clangd gate uses this accessor; tests verify unflushed snapshots and transaction safety.

### Fixed
- Removed private Store._doc reader usage from LSP snapshot tests and gate wiring.

## 0.16.1 â€” 2026-11-06

### Fixed
- Corrected SQLite snapshot dependency regression test to close its implicit read transaction before a subsequent edit.
- v0.14.0 CI revealed the failure on all three platforms; latest CI is pending.

## 0.16.0 â€” 2026-11-06

### Added
- Per-translation-unit include search paths from compile_commands.json.
- Nested header traversal inherits each translation unit's include directories.
- Tests for conflicting include names across independent compilation units and transitive headers.

### Limitations
- Multiple commands for one source are conservatively merged; -iquote/-I order is not modeled separately.
- No macro or conditional preprocessing, external include directories, response files or generated headers.

## 0.15.0 â€” 2026-11-06

### Added
- Conservative compile_commands.json reader for -I, -iquote and /I directories.
- DiagnosticGate resolves literal quoted includes using compilation database directories.
- Tests for arguments/command forms, missing database and workspace escape rejection.

### Limitations
- Include directories are merged across translation units, not yet scoped per compile command.
- No macros, conditional compilation, response files, system includes, external include paths or compiler driver evaluation.
- The implementation deliberately rejects include directories outside the workspace.

## 0.14.0 â€” 2026-11-06

### Added
- Optional snapshot_text reader for dependency discovery, allowing current unflushed SQLite include directives to drive transitive dependency scans.
- DiagnosticGate now forwards its Store snapshot reader to the dependency scanner.
- Regression tests for newly added and removed unsynced includes.

### Limitations
- Files must exist on disk to be enumerated by the scanner; newly created Store-only paths are not yet included.
- Macro/conditional includes and compile_commands.json are not yet supported.

## 0.13.1 â€” 2026-11-06

### Fixed
- JSON-RPC clangd gate now reads unchanged dependent documents from the SQLite Store snapshot instead of the potentially stale filesystem.
- Added regression test for an unflushed dependent source file.

### Limitations
- Include dependency discovery still reads on-disk directives, not unsynced Store content.
- Uses a private Store reader during validation; a public snapshot accessor is planned.

## 0.13.0 â€” 2026-11-06

### Added
- DiagnosticGate now opens discovered transitive literal-include dependent translation units and checks their diagnostics before committing header changes.
- Tests for dependent discovery, candidate refresh and atomic rejection on dependent-only regression.

### Limitations
- Dependency graph uses on-disk quoted includes; unchanged unsynced SQLite documents and compile_commands include paths are not represented.
- The gate is conservative, but not a complete compilation database or preprocessor-aware validator.

## 0.12.0 â€” 2026-11-06

### Added
- Bounded transitive dependency discovery for literal quoted C/C++ includes.
- Tests for direct/transitive inclusion, unrelated source changes, path traversal and scan limits.

### Limitations
- Dependency discovery is not yet wired into DiagnosticGate; this is groundwork only.
- Does not resolve macro includes, conditional compilation, -I search paths, generated headers or compile_commands.json.

## 0.11.1 â€” 2026-11-06

### Tests
- Added live clangd multi-file diagnostic rejection test on Ubuntu CI.
- Confirmed v0.11.0 Ubuntu CI passed with real clangd; Windows and macOS results were still pending at time of update.

## 0.11.0 â€” 2026-11-06

### Added
- Batch diagnostic gate opens all changed documents, collects baseline reports, then stages all candidate overlays before comparing diagnostics.
- Atomic multi-document regression tests for success and rejection.

### Limitations
- Only changed documents are checked; unchanged dependent translation units are not yet included.
- Live clangd compatibility and CI remain under verification.

## 0.10.1 â€” 2026-11-06

### Fixed
- JSON-RPC now returns structured errors for clangd diagnostic timeouts and connection failures instead of terminating the server.
- Added regression tests proving the server continues processing subsequent requests and preserves the SQLite revision.
- Normalized import order for Ruff.

## 0.10.0 â€” 2026-11-06

### Added
- Diagnostic validation for Store.undo and JSON-RPC history undo, preserving atomic rollback on rejection.
- Opt-in real clangd regression test for rejecting a newly introduced C syntax error.
- Linux CI now runs the live clangd diagnostic test in addition to lifecycle smoke test.

### Limitations
- Multi-file cross-dependency analysis is not yet implemented; live diagnostic test may expose clangd compatibility issues.

## 0.9.2 â€” 2026-11-06

### Fixed
- Ruff UP035 import errors in JSON-RPC and SQLite modules.
- CI now installs clangd on Ubuntu and runs the real LSP smoke test with Python 3.16.0a1.

## 0.9.1 â€” 2026-11-06

### Fixed
- Corrected clangd client lock initialization and serialized LSP writes.
- Added opt-in real clangd smoke test and version consistency regression test.
- Repaired escaped newline formatting in documentation.

## 0.9.0 â€” 2026-11-06
\n### Added\n- Optional `nextagent serve --clangd` mode enforcing diagnostic validation for JSON-RPC edits and symbol replacements.\n- Server lifecycle owns clangd client, and injected validators enable regression tests without clangd installation.\n\n### Limitations\n- Strict mode rejects unversioned or missing diagnostics; live clangd end-to-end verification is pending.\n- Cross-file diagnostic regressions and history undo are not covered by the gate.\n\n## 0.8.0 â€” 2026-11-06\n\n### Added\n- Opt-in pre-commit Store validator callback, executed inside the SQLite transaction before revision creation.\n- DiagnosticGate for clangd candidate overlays, versioned diagnostics and regression rejection.\n- Version-aware wait for clangd diagnostics and mock-based Store rollback tests.\n\n### Limitations\n- DiagnosticGate is Python API only; not wired into CLI/JSON-RPC. No live clangd integration test.\n- Fail closed on clangd diagnostics without a version; cross-file diagnostics are not yet validated.\n\n## 0.7.0 â€” 2026-11-06\n\n### Added\n- Diagnostic delta comparison with duplicate-aware error/warning detection.\n- Fail-closed validation of LSP diagnostic report versions.\n- Tests for new errors, warnings, duplicates, reordering and stale diagnostics.\n\n### Fixed\n- UTF-8 CJK position test now uses byte offsets, resolving the v0.5.0 CI failure.\n\n### Limitations\n- Validation is a standalone component; no Store pre-commit gate or rollback integration yet.\n\n## 0.6.0 â€” 2026-11-06\n\n### Added\n- Experimental clangd subprocess LSP client with initialize/shutdown lifecycle, request correlation and timeouts.\n- didOpen/didChange notifications, diagnostics capture and formatting requests.\n- Unit tests for missing executable, request correlation and workspace boundary.\n\n### Limitations\n- Not wired into editor JSON-RPC; formatting edits are returned, not applied. No diagnostics delta/rollback yet.\n\n## 0.5.0 â€” 2026-11-06\n\n### Added\n- Strict LSP position codecs for UTF-8, UTF-16 and UTF-32.\n- Binary Content-Length LSP framing with size limits and malformed-frame rejection.\n- Unicode and protocol framing regression tests.\n\n### Limitations\n- clangd subprocess, lifecycle, diagnostics and formatting are not yet integrated.\n\n## 0.4.0 â€” 2026-11-06\n\n### Added\n- Scoped nested C23 type/field navigation and ambiguity-safe selection.\n- Revision-guarded JSON-RPC `replace_symbol` for entire syntactic declarations.\n- Tests for scoped field lookup and stale revision rejection.\n\n### Limitations\n- No semantic name resolution, cross-file references or validation of replacement text.\n\n## 0.3.0 â€” 2026-11-06\n\n### Added\n- Initial C23 tree-sitter syntax navigation: outline, exact-name select, byte-to-Unicode offset mapping.\n- CLI and JSON-RPC outline/select operations, tests for Unicode and ambiguous names.\n\n### Limitations\n- Only top-level syntactic declarations; no cross-file resolution, LSP, refactoring or complete C23 semantic model.\n\n## 0.2.0 â€” 2026-11-06

### Added
- NextAgent public identity: `nextagent` CLI, `nextagent` Python package and distribution.
- Experimental newline-delimited JSON-RPC 2.0 stdio transport for open/view/edit/history/sync.
- Protocol and entrypoint regression tests.

### Changed
- Removed the legacy `aed` CLI/import name (breaking change in 0.x development phase).

### Not yet supported
- Semantic selectors, tree-sitter, LSP, MCP and Unix socket transport.

## 0.1.1 â€” 2026-11-06

### Fixed
- `sync` now compares the latest disk content against the imported baseline, rejecting observed external changes with `disk_conflict` before writing.
- Preserves existing file mode during atomic replacement.
- Adds regression tests for disk conflicts and repeated sync.

### Remaining limitations
- Multi-file disk writes are not transactional and race-free against external processes; three-way merge is scheduled for M4.

## 0.1.0 â€” 2026-11-06

### Added
- Initial M0 source-editing foundation: immutable Unicode snapshots and invertible changesets.
- SQLite WAL history, multi-file atomic revisions, stale revision checks and undo.
- Minimal CLI: view, edit, undo, history, sync.
- GitHub Actions CI definition for Python 3.16.0a1 on three operating systems.
- Tests, README and durable progress log in `docs/agent-plan.md`.

### Incomplete
- No three-way disk merge or cross-process file locking; no semantic addressing, LSP, MCP, JSON-RPC or brain.
