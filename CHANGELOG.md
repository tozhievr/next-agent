# Changelog

All product versions follow [Semantic Versioning](https://semver.org/).

## 0.15.0 — 2026-11-06

### Added
- Conservative compile_commands.json reader for -I, -iquote and /I directories.
- DiagnosticGate resolves literal quoted includes using compilation database directories.
- Tests for arguments/command forms, missing database and workspace escape rejection.

### Limitations
- Include directories are merged across translation units, not yet scoped per compile command.
- No macros, conditional compilation, response files, system includes, external include paths or compiler driver evaluation.
- The implementation deliberately rejects include directories outside the workspace.

## 0.14.0 — 2026-11-06

### Added
- Optional snapshot_text reader for dependency discovery, allowing current unflushed SQLite include directives to drive transitive dependency scans.
- DiagnosticGate now forwards its Store snapshot reader to the dependency scanner.
- Regression tests for newly added and removed unsynced includes.

### Limitations
- Files must exist on disk to be enumerated by the scanner; newly created Store-only paths are not yet included.
- Macro/conditional includes and compile_commands.json are not yet supported.

## 0.13.1 — 2026-11-06

### Fixed
- JSON-RPC clangd gate now reads unchanged dependent documents from the SQLite Store snapshot instead of the potentially stale filesystem.
- Added regression test for an unflushed dependent source file.

### Limitations
- Include dependency discovery still reads on-disk directives, not unsynced Store content.
- Uses a private Store reader during validation; a public snapshot accessor is planned.

## 0.13.0 — 2026-11-06

### Added
- DiagnosticGate now opens discovered transitive literal-include dependent translation units and checks their diagnostics before committing header changes.
- Tests for dependent discovery, candidate refresh and atomic rejection on dependent-only regression.

### Limitations
- Dependency graph uses on-disk quoted includes; unchanged unsynced SQLite documents and compile_commands include paths are not represented.
- The gate is conservative, but not a complete compilation database or preprocessor-aware validator.

## 0.12.0 — 2026-11-06

### Added
- Bounded transitive dependency discovery for literal quoted C/C++ includes.
- Tests for direct/transitive inclusion, unrelated source changes, path traversal and scan limits.

### Limitations
- Dependency discovery is not yet wired into DiagnosticGate; this is groundwork only.
- Does not resolve macro includes, conditional compilation, -I search paths, generated headers or compile_commands.json.

## 0.11.1 — 2026-11-06

### Tests
- Added live clangd multi-file diagnostic rejection test on Ubuntu CI.
- Confirmed v0.11.0 Ubuntu CI passed with real clangd; Windows and macOS results were still pending at time of update.

## 0.11.0 — 2026-11-06

### Added
- Batch diagnostic gate opens all changed documents, collects baseline reports, then stages all candidate overlays before comparing diagnostics.
- Atomic multi-document regression tests for success and rejection.

### Limitations
- Only changed documents are checked; unchanged dependent translation units are not yet included.
- Live clangd compatibility and CI remain under verification.

## 0.10.1 — 2026-11-06

### Fixed
- JSON-RPC now returns structured errors for clangd diagnostic timeouts and connection failures instead of terminating the server.
- Added regression tests proving the server continues processing subsequent requests and preserves the SQLite revision.
- Normalized import order for Ruff.

## 0.10.0 — 2026-11-06

### Added
- Diagnostic validation for Store.undo and JSON-RPC history undo, preserving atomic rollback on rejection.
- Opt-in real clangd regression test for rejecting a newly introduced C syntax error.
- Linux CI now runs the live clangd diagnostic test in addition to lifecycle smoke test.

### Limitations
- Multi-file cross-dependency analysis is not yet implemented; live diagnostic test may expose clangd compatibility issues.

## 0.9.2 — 2026-11-06

### Fixed
- Ruff UP035 import errors in JSON-RPC and SQLite modules.
- CI now installs clangd on Ubuntu and runs the real LSP smoke test with Python 3.16.0a1.

## 0.9.1 — 2026-11-06

### Fixed
- Corrected clangd client lock initialization and serialized LSP writes.
- Added opt-in real clangd smoke test and version consistency regression test.
- Repaired escaped newline formatting in documentation.

## 0.9.0 — 2026-11-06
\n### Added\n- Optional `nextagent serve --clangd` mode enforcing diagnostic validation for JSON-RPC edits and symbol replacements.\n- Server lifecycle owns clangd client, and injected validators enable regression tests without clangd installation.\n\n### Limitations\n- Strict mode rejects unversioned or missing diagnostics; live clangd end-to-end verification is pending.\n- Cross-file diagnostic regressions and history undo are not covered by the gate.\n\n## 0.8.0 — 2026-11-06\n\n### Added\n- Opt-in pre-commit Store validator callback, executed inside the SQLite transaction before revision creation.\n- DiagnosticGate for clangd candidate overlays, versioned diagnostics and regression rejection.\n- Version-aware wait for clangd diagnostics and mock-based Store rollback tests.\n\n### Limitations\n- DiagnosticGate is Python API only; not wired into CLI/JSON-RPC. No live clangd integration test.\n- Fail closed on clangd diagnostics without a version; cross-file diagnostics are not yet validated.\n\n## 0.7.0 — 2026-11-06\n\n### Added\n- Diagnostic delta comparison with duplicate-aware error/warning detection.\n- Fail-closed validation of LSP diagnostic report versions.\n- Tests for new errors, warnings, duplicates, reordering and stale diagnostics.\n\n### Fixed\n- UTF-8 CJK position test now uses byte offsets, resolving the v0.5.0 CI failure.\n\n### Limitations\n- Validation is a standalone component; no Store pre-commit gate or rollback integration yet.\n\n## 0.6.0 — 2026-11-06\n\n### Added\n- Experimental clangd subprocess LSP client with initialize/shutdown lifecycle, request correlation and timeouts.\n- didOpen/didChange notifications, diagnostics capture and formatting requests.\n- Unit tests for missing executable, request correlation and workspace boundary.\n\n### Limitations\n- Not wired into editor JSON-RPC; formatting edits are returned, not applied. No diagnostics delta/rollback yet.\n\n## 0.5.0 — 2026-11-06\n\n### Added\n- Strict LSP position codecs for UTF-8, UTF-16 and UTF-32.\n- Binary Content-Length LSP framing with size limits and malformed-frame rejection.\n- Unicode and protocol framing regression tests.\n\n### Limitations\n- clangd subprocess, lifecycle, diagnostics and formatting are not yet integrated.\n\n## 0.4.0 — 2026-11-06\n\n### Added\n- Scoped nested C23 type/field navigation and ambiguity-safe selection.\n- Revision-guarded JSON-RPC `replace_symbol` for entire syntactic declarations.\n- Tests for scoped field lookup and stale revision rejection.\n\n### Limitations\n- No semantic name resolution, cross-file references or validation of replacement text.\n\n## 0.3.0 — 2026-11-06\n\n### Added\n- Initial C23 tree-sitter syntax navigation: outline, exact-name select, byte-to-Unicode offset mapping.\n- CLI and JSON-RPC outline/select operations, tests for Unicode and ambiguous names.\n\n### Limitations\n- Only top-level syntactic declarations; no cross-file resolution, LSP, refactoring or complete C23 semantic model.\n\n## 0.2.0 — 2026-11-06

### Added
- NextAgent public identity: `nextagent` CLI, `nextagent` Python package and distribution.
- Experimental newline-delimited JSON-RPC 2.0 stdio transport for open/view/edit/history/sync.
- Protocol and entrypoint regression tests.

### Changed
- Removed the legacy `aed` CLI/import name (breaking change in 0.x development phase).

### Not yet supported
- Semantic selectors, tree-sitter, LSP, MCP and Unix socket transport.

## 0.1.1 — 2026-11-06

### Fixed
- `sync` now compares the latest disk content against the imported baseline, rejecting observed external changes with `disk_conflict` before writing.
- Preserves existing file mode during atomic replacement.
- Adds regression tests for disk conflicts and repeated sync.

### Remaining limitations
- Multi-file disk writes are not transactional and race-free against external processes; three-way merge is scheduled for M4.

## 0.1.0 — 2026-11-06

### Added
- Initial M0 source-editing foundation: immutable Unicode snapshots and invertible changesets.
- SQLite WAL history, multi-file atomic revisions, stale revision checks and undo.
- Minimal CLI: view, edit, undo, history, sync.
- GitHub Actions CI definition for Python 3.16.0a1 on three operating systems.
- Tests, README and durable progress log in `docs/agent-plan.md`.

### Incomplete
- No three-way disk merge or cross-process file locking; no semantic addressing, LSP, MCP, JSON-RPC or brain.
