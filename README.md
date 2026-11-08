# NextAgent

[![CI](https://github.com/tozhievr/next-agent/actions/workflows/ci.yml/badge.svg)](https://github.com/tozhievr/next-agent/actions/workflows/ci.yml)

**A headless, transactional code editor for AI coding agents.**
Agents edit by *symbol* or *codepoint range* instead of re-reading whole files, counting line numbers or generating throw-away scripts. Every change is an atomic, undoable revision, and can optionally be vetted by clangd before it is committed.

Written in **C23**, built with the **nextc** compiler for Windows x86-64. No Windows SDK, Visual Studio, Python or SQLite is required: the result is a single `nextagent.exe` (~170 KB) that talks only to kernel32.

> **Status: 0.24.0, early development.** Version 0.20.0 was a full rewrite of the former Python implementation (see [Upgrading](#upgrading-from-019)). The design follows the *aed architecture v1.1* specification; `aed` is only the historical name of that document.

## Contents

- [Why](#why) · [Features](#features) · [Build](#build) · [Quick start](#quick-start)
- [CLI reference](#cli-reference) · [JSON-RPC](#json-rpc-server) · [MCP](#mcp-bridge)
- [clangd diagnostic gate](#clangd-diagnostic-gate) · [Storage model](#storage-model) · [Source layout](#source-layout)
- [Upgrading](#upgrading-from-019) · [Limitations](#limitations-and-not-implemented) · [License](#license)
- [Install](#install) · **For AI models:** [docs/AGENT_GUIDE.md](docs/AGENT_GUIDE.md) — how to drive the editor

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
| Languages | `serve --lsp`: Python, Rust, Go, TypeScript, ... via language servers (symbols and diagnostic gate); incremental C23 outline cache. |
| clangd gate | Pre-commit diagnostics comparison, literal-include dependent discovery, `compile_commands.json` include dirs, fail-closed freshness, build-state recheck inside the write lock. |
| Sync | Explicit `sync` writes committed text to disk with baseline conflict checks. |

## Install

One line (PowerShell, Windows x64): downloads the latest release, verifies its SHA-256 and adds it to your user `PATH`.

```powershell
irm https://raw.githubusercontent.com/tozhievr/next-agent/main/install.ps1 | iex
```

Linux x86-64: `curl -fsSL https://raw.githubusercontent.com/tozhievr/next-agent/main/install.sh | sh` (installs to `~/.local/bin`; `NEXTAGENT_VERSION`, `NEXTAGENT_DIR` work too).

A specific version or directory: `$env:NEXTAGENT_VERSION = "v0.24.0"; $env:NEXTAGENT_DIR = "C:\tools\nextagent"` before running. Releases are published automatically when a `v*` tag is pushed (see [`release.yml`](.github/workflows/release.yml)); the tag must equal `NA_VERSION` in `src/util.h`.

## Skill for Claude Code

The release also carries `nextagent-skill-<version>.zip`: a Claude Code skill (`SKILL.md` plus a command reference)
that teaches the model to edit through `nextagent` (one `apply` script instead of many Edit calls, one function
instead of a whole file, `locate` for questions) and a one-command installer. Unpack it and run `install-skill.sh`
or `install-skill.ps1` (user-wide, or `--project` / `-Project` for one repository); see `skill/README.md`. The
numbers behind the claim are in [docs/BENCHMARK.md](docs/BENCHMARK.md), with the limits of the method.

## Build

**Recommended (Windows and Linux, identical behaviour):** clang + the vendored libc23, no system libc or SDK:

```sh
python build.py test --target windows   # or linux (WSL runs the tests when on Windows)
python tests/parity.py build/linux/nextagent   # output must equal tests/parity.golden
```

Needs clang, lld, llvm and ninja. CI builds and tests both targets and publishes `windows-x64.zip` and `linux-x64.tar.gz` on every `v*` tag.

Alternative Windows builds:

Two compilers are supported; both produce the same `build/nextagent.exe`.

| Compiler | Needs | Select |
| --- | --- | --- |
| **nextc** (default) | only nextc and its bundled libc23; no Windows SDK / Visual Studio | `./build.ps1` |
| **clang** | clang plus the Windows SDK/MSVC runtime (e.g. Visual Studio Build Tools) | `./build.ps1 -Compiler clang` |

```powershell
./build.ps1                       # -> build/nextagent.exe (nextc)
./build.ps1 test                  # builds and runs build/tests.exe
./build.ps1 test -Compiler clang  # same with clang (release builds use this)
```

```sh
./build.sh [test]                 # Git Bash / MSYS; NA_COMPILER=clang to use clang
```

If a compiler is not on `PATH`, point to it: `$env:NEXTC = "D:\nextc\src\.build\nextc.exe"` or `$env:CLANG = "C:\Program Files\LLVM\bin\clang.exe"`.
The clang path is verified here only by a syntax/warnings check; the full link runs in CI.
The suite has 298 checks; the live-clangd ones are skipped when `clangd` is missing.

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

view PATH [--lines A-B|--symbol NAME] [--raw]   text and current revision
merge [PATH...] [--ours|--theirs] [--dry-run]   reconcile files changed outside nextagent (3-way merge)
callers NAME [--depth N] / callees NAME         call graph (callers via references, callees from the index)
context QUERY [--budget TOKENS]                   best code for a question as one pack
locate QUERY [--limit N]                        ranked hybrid search (BM25 + names + phrase)
search QUERY [--word] [-i] [--context N]         grep over the workspace index
apply [FILE|-]                                   many edits, one revision, plain-text script (@@@ replace/lines/insert/symbol/create)
create PATH --new-file F / insert PATH --before-symbol NAME --new-file F
replace PATH --old S --new S [--all]              literal replacement
replace PATH --lines A-B --new-file F             replace whole lines
replace-symbol PATH NAME --new-file F             rewrite one symbol
patch [FILE|-]                    edits [{path,old,new,all}] across files, one revision
references/rename/add-param/find semantic through language servers (serve --lsp)
global: --brief (compact output), --no-sync (do not write to disk)
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

## Other languages (language servers)

`serve --lsp` / `mcp --lsp` start a *language server hub*. Files are routed by extension to a server that is started on first use:

| Languages | Server (first one found on `PATH`) |
| --- | --- |
| C, C++ | `clangd` |
| Python | `pyright-langserver`, `basedpyright-langserver`, `pylsp` |
| Rust | `rust-analyzer` |
| Go | `gopls` |
| TypeScript, JavaScript | `typescript-language-server` |
| Zig, Lua | `zls`, `lua-language-server` |

With the hub, `outline`, `select` and `replace_symbol` work on non-C files through `textDocument/documentSymbol` (`nested` returns children with `scope`), and the pre-commit diagnostic gate also vets edits to those files. Edits reach the server as ranged `didChange` notifications when it supports incremental sync. `lsp_servers` lists what is configured, installed and running.
Verified live: clangd (C/C++), pyright (Python), typescript-language-server (TypeScript/JavaScript; the project must have `typescript` 5.x in its `node_modules`, otherwise the server fails to initialize and nextagent sees no symbols). On Windows start npm-installed servers as `node <path>/langserver.index.js --stdio` (a `.cmd` shim cannot be spawned directly). `tests/lspcheck.py` re-runs the check.

Add or override servers in `<root>/.aed/lsp.json`:

```json
{"servers": [{"name": "mypy", "command": "pylsp", "args": "", "extensions": [".py"], "language": "python"}]}
```

Servers installed as `.cmd`/`.bat` shims (typical for npm packages on Windows) must be wrapped, e.g. `"command": "cmd", "args": "/c typescript-language-server --stdio"`.

### Embedding model (optional)

`locate` works without any model (BM25 plus embeddings learned from your own code). To add a real
model, create `.aed/embed.json`:

```
{"url": "http://127.0.0.1:11434/v1/embeddings", "model": "nomic-embed-text"}   # Ollama, llama.cpp server, LM Studio, vLLM...
{"command": "my-embedder", "args": "--fast"}                                    # any program: {"input":[...]} -> {"embeddings":[[...]]} per line
```

Plain HTTP only, no DNS and no TLS (put a local proxy in front of a remote or https service, or use
`command`). Each chunk is sent once and cached in `.aed/embed.cache`; `locate --embed N` caps new chunks per query.

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

- **Not implemented:** branches, offline brain, a file-watcher thread (outside changes are found by stat on the next query), async writer, socket transport, M5b.
- The scanner is tolerant rather than a full parser; exotic declarators may be misreported. `select` is syntax-only, with no cross-file resolution.
- clangd gate: with clangd 23 an *unchanged* dependent is not re-diagnosed when an overlay header changes *semantically* (e.g. a signature), so only errors in the edited documents or syntax errors in headers are caught. Macro includes, conditional compilation, system include paths and generated headers are not followed.
- `references` / `rename` / `find_symbols` are semantic through language servers (`serve --lsp`); `add_param` and `add_include` are still C-only and token-based. Without a server, C falls back to token search; other languages need a server.

See [CHANGELOG.md](CHANGELOG.md) and [docs/agent-plan.md](docs/agent-plan.md).

## License

MIT, see [LICENSE](LICENSE).
