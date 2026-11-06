# NextAgent

[![CI](https://github.com/tozhievr/next-agent/actions/workflows/ci.yml/badge.svg)](https://github.com/tozhievr/next-agent/actions/workflows/ci.yml)

**NextAgent** is a headless, transactional code editor built for AI coding agents. Its goal is to make reliable edits without repeatedly reading entire files, calculating line numbers, and generating temporary scripts.

**Current version: 0.10.0 (early development).** The architecture is based on the supplied *aed architecture v1.1* design. **NextAgent** is the product name, `nextagent` is both the installed command and Python import package. `aed` is only the historical name of the specification.

## Features available today

- Immutable Unicode document snapshots, codepoint-offset changesets, inversion and position mapping.
- SQLite WAL-backed atomic multi-file edit transactions, optimistic revision checks and append-only undo history (including opt-in diagnostic validation of undo).
- Explicit filesystem sync with disk-baseline conflict checks (not a three-way merge).
- C23 tree-sitter symbol outline, scoped struct/union/enum fields and guarded whole-declaration replacement (syntax only, not LSP symbol resolution).
- CLI and **experimental JSON-RPC 2.0 over newline-delimited stdio** for basic open/view/edit/history/sync methods.
- LSP foundation: strict UTF-8/UTF-16/UTF-32 position conversions and binary Content-Length message framing (experimental clangd subprocess client available).\n- Tests and CI on Python **3.16.0a1** (Linux, Windows, macOS).

**Not implemented yet:** full semantic selectors, automatic clangd integration into editing, diagnostics delta and rollback, MCP bridge, offline brain, async writer, socket transport, branches and full disk merge. Current edits use explicit Unicode offsets.

`nextagent --root ./project serve --clangd` also validates `history` undo operations before committing their inverse edits. Cross-file semantic impact is not yet covered.\n\n## Install

Python >= 3.13 is supported by package metadata. CI is pinned to Python 3.16.0a1. Until PR #1 is merged, install from the feature branch shown below.

```sh
git clone --branch feat/aed-m0-foundation https://github.com/tozhievr/next-agent.git
cd next-agent
python -m pip install -e '.[dev]'
nextagent --version
python -m pytest -q
```

## CLI quick start

```sh
mkdir example
printf 'hello\n' > example/demo.c
nextagent --root example view demo.c\nnextagent --root example outline demo.c
printf '{"if_rev":0,"ops":[{"path":"demo.c","start":0,"end":5,"text":"world"}]}' | nextagent --root example edit
nextagent --root example history
nextagent --root example sync demo.c
nextagent --root example undo
```

The `edit` request accepts `ops` with `path`, `start`, `end`, `text`, plus optional `if_rev` and `label`. All offsets are zero-based **Unicode codepoints** (not bytes). All operations in a batch are resolved against the original buffer. Edits commit to SQLite first; `sync` writes them to disk.

## Agent integration: experimental JSON-RPC

Run `nextagent --root ./project serve` and send **one JSON object per line** to stdin:

```json
{"jsonrpc":"2.0","id":1,"method":"open"}
{"jsonrpc":"2.0","id":2,"method":"edit","params":{"if_rev":0,"ops":[{"path":"src/main.c","start":0,"end":0,"text":"int main(void) { return 0; }"}]}}
{"jsonrpc":"2.0","id":3,"method":"view","params":{"target":"src/main.c"}}\n{"jsonrpc":"2.0","id":5,"method":"outline","params":{"target":"src/main.c"}}\n{"jsonrpc":"2.0","id":6,"method":"select","params":{"target":"src/main.c","name":"main","kind":"function"}}
{"jsonrpc":"2.0","id":4,"method":"sync","params":{"op":"flush"}}
```

C23 `outline` and `select` are syntax-only; `outline` supports optional `nested=true` for tagged type definitions and their fields. `select` accepts an optional `scope`. `replace_symbol` replaces one selected declaration with mandatory workspace `if_rev` for stale-edit protection. Ambiguous names are rejected; there is no cross-file name resolution yet.\n\nReplies are JSON-RPC 2.0 responses, one per line. Notifications (requests without `id`) produce no reply. `history` supports `op=log` or `op=undo`. This framing is **not** the Content-Length framing used by LSP and is **not** an MCP server. More transport options are planned.

## Experimental clangd client\n\nThe Python API `nextagent.lsp.client.ClangdClient` can launch a locally installed `clangd`, perform the LSP initialize handshake, open/change C documents, collect published diagnostics and request formatting edits. A separate `nextagent.lsp.validation` module compares diagnostics as multisets and rejects new errors/warnings; it requires version-matched diagnostic reports to avoid stale-result acceptance. A pre-commit `DiagnosticGate` can now be supplied as `Store.edit(..., validator=DiagnosticGate(root, clangd_client))`. It validates candidate overlays before SQLite commit and rejects diagnostic regressions. This is opt-in: launch `nextagent --root ./project serve --clangd` to require the diagnostic gate for JSON-RPC `edit` and `replace_symbol`. A locally installed clangd is required. Plain `serve` remains unvalidated. Diagnostics without a version fail closed; clangd may not publish versioned diagnostics in all configurations. It requires version-tagged clangd diagnostics; missing/unversioned reports fail closed.\n\nThe client **does not automatically apply formatting**, integrate into `nextagent serve`, or validate edits before commit. Install `clangd` separately and provide its executable on PATH.\n\n## Safety and roadmap

`sync` detects external file changes relative to the imported baseline and raises `disk_conflict`. It does not provide cross-process locking, crash-atomic multi-file disk writes, or three-way merge. Do not use it for concurrently edited or untrusted workspaces.

See [docs/agent-plan.md](docs/agent-plan.md) for completed tasks, test evidence, versioning policy, risks and next milestones. MIT license.
