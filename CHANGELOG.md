# Changelog

All product versions follow [Semantic Versioning](https://semver.org/).

## 0.3.0 — 2026-11-06\n\n### Added\n- Initial C23 tree-sitter syntax navigation: outline, exact-name select, byte-to-Unicode offset mapping.\n- CLI and JSON-RPC outline/select operations, tests for Unicode and ambiguous names.\n\n### Limitations\n- Only top-level syntactic declarations; no cross-file resolution, LSP, refactoring or complete C23 semantic model.\n\n## 0.2.0 — 2026-11-06

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
