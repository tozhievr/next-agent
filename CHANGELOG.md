# Changelog

All product versions follow [Semantic Versioning](https://semver.org/).

## 0.8.0 — 2026-11-06\n\n### Added\n- Opt-in pre-commit Store validator callback, executed inside the SQLite transaction before revision creation.\n- DiagnosticGate for clangd candidate overlays, versioned diagnostics and regression rejection.\n- Version-aware wait for clangd diagnostics and mock-based Store rollback tests.\n\n### Limitations\n- DiagnosticGate is Python API only; not wired into CLI/JSON-RPC. No live clangd integration test.\n- Fail closed on clangd diagnostics without a version; cross-file diagnostics are not yet validated.\n\n## 0.7.0 — 2026-11-06\n\n### Added\n- Diagnostic delta comparison with duplicate-aware error/warning detection.\n- Fail-closed validation of LSP diagnostic report versions.\n- Tests for new errors, warnings, duplicates, reordering and stale diagnostics.\n\n### Fixed\n- UTF-8 CJK position test now uses byte offsets, resolving the v0.5.0 CI failure.\n\n### Limitations\n- Validation is a standalone component; no Store pre-commit gate or rollback integration yet.\n\n## 0.6.0 — 2026-11-06\n\n### Added\n- Experimental clangd subprocess LSP client with initialize/shutdown lifecycle, request correlation and timeouts.\n- didOpen/didChange notifications, diagnostics capture and formatting requests.\n- Unit tests for missing executable, request correlation and workspace boundary.\n\n### Limitations\n- Not wired into editor JSON-RPC; formatting edits are returned, not applied. No diagnostics delta/rollback yet.\n\n## 0.5.0 — 2026-11-06\n\n### Added\n- Strict LSP position codecs for UTF-8, UTF-16 and UTF-32.\n- Binary Content-Length LSP framing with size limits and malformed-frame rejection.\n- Unicode and protocol framing regression tests.\n\n### Limitations\n- clangd subprocess, lifecycle, diagnostics and formatting are not yet integrated.\n\n## 0.4.0 — 2026-11-06\n\n### Added\n- Scoped nested C23 type/field navigation and ambiguity-safe selection.\n- Revision-guarded JSON-RPC `replace_symbol` for entire syntactic declarations.\n- Tests for scoped field lookup and stale revision rejection.\n\n### Limitations\n- No semantic name resolution, cross-file references or validation of replacement text.\n\n## 0.3.0 — 2026-11-06\n\n### Added\n- Initial C23 tree-sitter syntax navigation: outline, exact-name select, byte-to-Unicode offset mapping.\n- CLI and JSON-RPC outline/select operations, tests for Unicode and ambiguous names.\n\n### Limitations\n- Only top-level syntactic declarations; no cross-file resolution, LSP, refactoring or complete C23 semantic model.\n\n## 0.2.0 — 2026-11-06

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
