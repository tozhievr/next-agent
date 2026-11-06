# NextAgent

[![CI](https://github.com/tozhievr/next-agent/actions/workflows/ci.yml/badge.svg)](https://github.com/tozhievr/next-agent/actions/workflows/ci.yml)

**A headless, transactional code editor for AI coding agents.**
Agents edit by *symbol* or *codepoint range* instead of re-reading whole files, counting line numbers or generating throw-away scripts. Every change is an atomic, undoable revision, and can optionally be vetted by clangd before it is committed.

Written in **C23**, built with the **nextc** compiler for Windows x86-64. No Windows SDK, Visual Studio, Python or SQLite is required: the result is a single `nextagent.exe` (~170 KB) that talks only to kernel32.

> **Status: 0.20.0, early development.** Version 0.20.0 is a full rewrite of the former Python implementation (see [Upgrading](#upgrading-from-019)). The design follows the *aed architecture v1.1* specification; `aed` is only the historical name of that document.

## Contents

- [Why](#why) · [Features](#features) · [Build](#build) · [Quick start](#quick-start)
- [CLI reference](#cli-reference) · [JSON-RPC](#json-rpc-server) · [MCP](#mcp-bridge)
- [clangd diagnostic gate](#clangd-diagnostic-gate) · [Storage model](#storage-model) · [Source layout](#source-layout)
- [Upgrading](#upgrading-from-019) · [Limitations](#limitations-and-not-implemented) · [License](#license)

## Why

Typical agent edit loops are wasteful and fragile: read a file, guess line numbers, write a patch script, hope nothing changed in between. NextAgent replaces that with:

- **Stable addressing** – Unicode *codepoint* offsets or C symbol names, never byte offsets or line numbers.
- **Transactions** – a batch touching many files commits as one revision or not at all.
- **Optimistic concurrency** – pass `if_rev`; stale edits are rejected instead of clobbering newer work.
- **Reversibility** – append-only history with undo of any revision and redo.
- **Safety net** – an optional gate refuses edits that introduce new compiler diagnostics.

## Features

| Area | What you get |
| --- | --- |
| Documents | Immutable snapshots, codepoint-offset changesets, inversion, position mapping, BLAKE2b-16 hashes. CRLF preserved exactly. |
| Store | Append-only checksummed journal, torn-tail recovery, exclusive cross-process write lock, `stale_rev` / `stale_snapshot` / `disk_conflict` protection. |
| History | `log`, `undo` of the head or of any revision (rebased over later edits, `undo_conflict` on overlap), `redo`. |
| C23 navigation | Built-in tolerant scanner: `outline`, `--nested` struct/union/enum fields, `select` by name/kind/scope, guarded `replace_symbol`. Syntax only, no name resolution. |
| Interfaces | CLI, JSON-RPC 2.0 over newline-delimited stdio, MCP stdio bridge. |
| clangd gate | Pre-commit diagnostics comparison, literal-include dependent discovery, `compile_commands.json` include dirs, fail-closed freshness, build-state recheck inside the write lock. |
| Sync | Explicit `sync` writes committed text to disk with baseline conflict checks. |

## Build

Requirements: the **nextc** compiler (C23, bundled libc23). Optionally `clangd` on `PATH` for the live gate tests.

```powershell
./build.ps1            # -> build/nextagent.exe
./build.ps1 test       # builds and runs build/tests.exe
```

```sh
./build.sh [test]      # same, from Git Bash / MSYS
```

If `nextc` is not on `PATH`, point to it: `$env:NEXTC = "D:\nextc\src\.build\nextc.exe"`.
The suite has 158 checks; the live-clangd ones are skipped when `clangd` is missing.

## Quick start

```powershell
mkdir example; "int add(int a, int b) { return a + b; }`n" | Set-Content example/demo.c

nextagent --root example outline demo.c
# {"symbols":[{"name":"add","kind":"function","start":0,"end":39,...}]}

'{"if_rev":0,"label":"rename","ops":[{"path":"demo.c","start":4,"end":7,"text":"sum"}]}' | nextagent --root example edit
# {"rev":1,"paths":["demo.c"]}

nextagent --root example view demo.c     # shows the edited text (not on disk yet)
nextagent --root example undo            # revision 2 = inverse of revision 1
nextagent --root example redo            # revision 3 = re-applies it
nextagent --root example sync            # writes demo.c to disk
```

## CLI reference

```
nextagent [--root DIR] [--version] <command>

view PATH                        text and current revision
outline PATH [--nested]          top-level symbols (+ tagged types and fields)
select PATH NAME [--kind K] [--scope S]
edit [--request FILE]            JSON request from file or stdin
undo [--rev N]                   undo head, or revision N
redo                             re-apply the last undone head
history [--limit N]              revision log, newest first
sync [PATH...]                   flush to disk (all documents by default)
serve [--clangd[=EXE]]           JSON-RPC server on stdio
mcp   [--clangd[=EXE]]           MCP server on stdio
```

`edit` request: `{"ops":[{"path","start","end","text"}...], "if_rev": N?, "label": "..."?}`.
Offsets are zero-based Unicode codepoints and every op refers to the *original* text, so ops in one batch must not overlap. Output is compact JSON on stdout, errors go to stderr with exit code 1.

## JSON-RPC server

`nextagent --root DIR serve` reads **one JSON object per line** and writes one reply per line. This is *not* LSP Content-Length framing.

```json
{"jsonrpc":"2.0","id":1,"method":"open"}
{"jsonrpc":"2.0","id":2,"method":"edit","params":{"if_rev":0,"ops":[{"path":"src/main.c","start":0,"end":0,"text":"int main(void) { return 0; }\n"}]}}
{"jsonrpc":"2.0","id":3,"method":"select","params":{"target":"src/main.c","name":"main","kind":"function"}}
{"jsonrpc":"2.0","id":4,"method":"replace_symbol","params":{"target":"src/main.c","name":"main","replacement":"int main(void) { return 1; }","if_rev":1}}
{"jsonrpc":"2.0","id":5,"method":"history","params":{"op":"undo","rev":1}}
{"jsonrpc":"2.0","id":6,"method":"sync"}
```

| Method | Params | Result |
| --- | --- | --- |
| `open` | – | `{name, version, rev}` |
| `view` | `target` | `{rev, path, text}` |
| `outline` | `target`, `nested?` | `{symbols}` |
| `select` | `target`, `name`, `kind?`, `scope?` | one symbol |
| `replace_symbol` | `target`, `name`, `replacement`, `if_rev` (= head), `kind?`, `scope?`, `label?` | `{rev, paths}` |
| `edit` | `ops`, `if_rev?`, `label?` | `{rev, paths}` |
| `history` | `op` = `log` (`n?`) \| `undo` (`rev?`) \| `redo` | log array or `{rev, paths}` |
| `sync` | `paths?` | `{ok, written}` |

Errors: `-32700` parse, `-32600` invalid request, `-32601` unknown method, `-32602` invalid params or a domain error (`stale_rev`, `symbol_not_found`, `ambiguous_symbol`, `undo_conflict`, `disk_conflict`, `diagnostics_regression`, ...). Requests without `id` are notifications and get no reply.

## MCP bridge

`nextagent --root DIR mcp` speaks the Model Context Protocol over stdio (newline-delimited JSON): `initialize`, `tools/list`, `tools/call`, `ping`. Tools: `view`, `outline`, `select`, `replace_symbol`, `edit`, `history`, `sync`, with the same parameters as above. Failures are returned as tool results with `isError: true`.

Example client configuration:

```json
{ "mcpServers": { "nextagent": { "command": "D:/next-agent/build/nextagent.exe",
                                  "args": ["--root", "D:/project", "mcp", "--clangd"] } } }
```

## clangd diagnostic gate

With `serve --clangd` / `mcp --clangd`, every `edit`, `replace_symbol`, `undo` and `redo` is validated **before** it is committed:

1. Changed C/C++ files and *literal-include dependents* (found from `#include "..."` with `-I`, `-iquote`, `/I` from `compile_commands.json` in the root or `build/`) are opened in clangd with their old text; unflushed headers are overlaid.
2. Diagnostics are collected, the new text is applied, diagnostics are collected again.
3. Diagnostics are compared as multisets by (severity, code, message). Any *added* diagnostic at warning severity or worse rejects the edit with `diagnostics_regression`.
4. Reports must carry the expected document version, otherwise the gate fails closed (`diagnostics_unavailable_or_stale`).
5. Inside the write lock the compilation database and dependency state are hashed again; a change rejects the commit as `stale_validation`.

Nothing is written on rejection. Plain `serve` is unvalidated.

## Storage model

State lives in `<root>/.aed/`:

- `journal.aedj` – header `AEDJ1\n`, then records `[u32 length][JSON payload][8-byte BLAKE2b prefix checksum]`. Record types: *base* (disk baseline), *commit* (one revision with per-document forward/inverse edits), *flush*. A torn tail is detected by the checksum and discarded on the next write.
- `journal.lock` – exclusive lock file; writers take it, replay the journal tail and only then append, so several processes can share a workspace.

`.git` and `.aed` components, drive/stream (`:`) components, `..` escapes and symlink/junction targets are rejected. Workspace files are only modified by `sync`.

## Source layout

```
src/util.*      buffers, errors, UTF-8, BLAKE2b, path rules
src/json.*      JSON DOM and compact writer
src/platform.*  Windows layer: files, atomic write, lock, processes and pipes
src/core.*      snapshots and changesets
src/store.*     journal store, history, flush
src/c23.*       C23 scanner: outline / select / replace_symbol
src/lsp_*.c     position codec, framing, diagnostics, compilation DB, clangd client, gate
src/rpc.*       JSON-RPC and MCP
src/main.c      CLI
tests/tests.c   test suite
```

## Upgrading from 0.19

- State moved from `.aed/state.sqlite3` to `.aed/journal.aedj`. **Old SQLite state is not read or migrated** – run `sync` with the old version first.
- CRLF is preserved exactly (the Python version normalised it).
- The Python package, `pyproject.toml` and pytest suite are gone; use `build.ps1`.

## Limitations and not implemented

- **Not implemented:** branches, offline brain, M3 refactorings (rename / references / add-param), three-way merge and file watcher, async writer, socket transport, M5b.
- The scanner is tolerant rather than a full parser; exotic declarators may be misreported. `select` is syntax-only, with no cross-file resolution.
- clangd gate: with clangd 23 an *unchanged* dependent is not re-diagnosed when an overlay header changes *semantically* (e.g. a signature), so only errors in the edited documents or syntax errors in headers are caught. Macro includes, conditional compilation, system include paths and generated headers are not followed.
- The GitHub Actions workflow was rewritten for nextc but has **not been run**.

See [CHANGELOG.md](CHANGELOG.md) and [docs/agent-plan.md](docs/agent-plan.md).

## License

MIT, see [LICENSE](LICENSE).
