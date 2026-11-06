# aed — headless code editor for AI agents

[![CI](https://github.com/tozhievr/next-agent/actions/workflows/ci.yml/badge.svg)](https://github.com/tozhievr/next-agent/actions/workflows/ci.yml)

`aed` is a transactional source-code editing engine designed for AI agents. The architectural target is semantic selectors via tree-sitter and LSP, compact results, SQLite revision history, and an optional offline `brain` for token-efficient navigation.

**Status: v0.1.0 (initial M0 prototype).** This build implements immutable Unicode snapshots, codepoint-based changesets, atomic multi-file transactions, SQLite persistence, history, inverse undo, a minimal CLI and explicit disk synchronization. **Semantic selection, LSP, MCP, JSON-RPC, external disk conflict handling, async writer and brain are NOT implemented.**

## Installation

Install for local development using Python >= 3.13:

```sh
python -m pip install -e '.[dev]'
python -m pytest -q
aed --version
```

GitHub Actions targets **Python 3.16.0a1** exactly (Ubuntu, Windows, macOS). This is a pre-release interpreter and jobs may fail during setup if an exact cached runtime is unavailable.

## Quick start

```sh
mkdir example
printf 'hello\n' > example/demo.c
aed --root example view demo.c
printf '{"if_rev":0,"ops":[{"path":"demo.c","start":0,"end":5,"text":"world"}]}' | aed --root example edit
aed --root example history
aed --root example sync demo.c
aed --root example undo
```

An edit request contains `ops` (objects with `path`, `start`, `end`, `text`) and optional `if_rev` and `label`. Offsets refer to zero-based **Python Unicode codepoints**, not byte indices or line numbers. All edits address the same original version of their respective buffers; each batch commits once. The editor owns its buffers; `sync` writes to the filesystem explicitly.

**Safety warning:** this prototype does not detect external modifications before `sync` and may overwrite concurrently modified files. Do not run against untrusted workspaces or concurrently edited files.

See [docs/agent-plan.md](docs/agent-plan.md) for milestones, test evidence, risks and version policy. The project follows the supplied *aed architecture v1.1* specification as its product target. Licensed under MIT.
