# Changelog

All product versions follow [Semantic Versioning](https://semver.org/).

## 0.1.0 — 2026-11-06

### Added
- Initial M0 source-editing foundation: immutable Unicode snapshots and invertible changesets.
- SQLite WAL history, multi-file atomic revisions, stale revision checks and undo.
- Minimal CLI: view, edit, undo, history, sync.
- GitHub Actions CI definition for Python 3.16.0a1 on three operating systems.
- Tests, README and durable progress log in `docs/agent-plan.md`.

### Incomplete
- External disk modification conflict detection; no semantic addressing, LSP, MCP, JSON-RPC or brain.
