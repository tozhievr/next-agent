# NextAgent

[![CI](https://github.com/tozhievr/next-agent/actions/workflows/ci.yml/badge.svg)](https://github.com/tozhievr/next-agent/actions/workflows/ci.yml)

**A headless, transactional code editor for AI coding agents, with the search and navigation around it.**
One small binary (about 170 KB, no runtime, no SDK), the same behaviour on Windows and Linux.

A coding agent normally reads whole files, counts line numbers and sends one edit per place. NextAgent lets it:

- **read less**: one function (`view --symbol`), a ranked answer to a question (`locate`, `context`), the callers of a
  function (`callers`);
- **change many places in one call**: a plain-text script (`apply`), one-liners (`sub`, `line`, `append`), or a rename
  across the workspace (`rename`), each one revision;
- **undo anything**: every change is an entry of an append-only, checksummed history (`undo`, `redo`, `history`);
- **share the files with people and git**: changes made outside nextagent are merged (three-way), not overwritten.

It works from a shell tool (CLI), as an MCP server, or as a JSON-RPC server, and ships a Claude Code skill.

> **Version 0.44.0.** Written in C23 (clang + a vendored libc23). about 430 checks on Windows and 415 on Linux, plus a
> byte-for-byte cross-platform parity test. See [CHANGELOG.md](CHANGELOG.md).

## Contents

[What it saves](#what-it-saves) · [Install](#install) · [Quick start](#quick-start) · [Commands](#commands) ·
[Languages](#languages-and-language-servers) · [Search](#search-by-meaning) · [Use it from an AI tool](#use-it-from-an-ai-tool) ·
[Safety and storage](#safety-and-storage) · [clangd gate](#clangd-diagnostic-gate) · [Build](#build) ·
[Source layout](#source-layout) · [Limitations](#limitations)

## What it saves

A modelled benchmark on QuickJS (88 000 lines of C, six tasks; [docs/BENCHMARK.md](docs/BENCHMARK.md) states the
method): what the model has to write plus read, with Grep/Read/Edit-style tools against nextagent.

| task | calls | tokens (generic tools) | tokens (nextagent) |
|---|---|---|---|
| find where something lives | 2 -> 1 | 1415 | 180 |
| read one function | 3 -> 1 | 2501 | 930 |
| rename a function, 23 sites in 4 files | 9 -> 1 | 3066 | 35 |
| six insertions in three files | 9 -> 1 | 2182 | 115 |
| **all six tasks** | **29 -> 6** | **13 403** | **5 208** |

About 2.6x fewer tokens overall; the gain is in changes that touch many places, reading tasks gain 1.6-8x and a
budgeted `context` pack costs the same as reading the code. It is a model, not a measurement of a live agent, and a
careful shell one-liner (`grep | xargs sed`) is cheaper for a rename, but it checks nothing and cannot be undone.

## Install

**Binary.** Windows x64 (PowerShell) downloads the latest release, verifies its SHA-256 and adds it to your user `PATH`:

```powershell
irm https://raw.githubusercontent.com/tozhievr/next-agent/main/install.ps1 | iex
```

Linux x86-64: `curl -fsSL https://raw.githubusercontent.com/tozhievr/next-agent/main/install.sh | sh` (installs to
`~/.local/bin`). `NEXTAGENT_VERSION` (a tag such as `v0.40.0`) and `NEXTAGENT_DIR` choose another version or directory.
Releases are published by [`release.yml`](.github/workflows/release.yml) when a `v*` tag is pushed; the tag must equal
`NA_VERSION` in `src/util.h`.

**Claude Code skill.** The release also carries `nextagent-skill-<version>.zip` (`SKILL.md` + command reference +
installers). Unpack it and run `install-skill.sh` or `install-skill.ps1` (user-wide, or `--project` / `-Project` for one
repository); see [skill/README.md](skill/README.md). The skill needs nextagent 0.39 or newer.

Check: `nextagent --version`. Add `.aed/` to the `.gitignore` of every project you use it in.

## Quick start

```sh
printf 'int add(int a, int b) {\n    return a + b;\n}\n\nint main(void) {\n    return add(1, 2);\n}\n' > demo.c

nextagent --brief outline demo.c            # 1 function add / 5 function main
nextagent view demo.c --symbol add --raw    # only that function
nextagent rename add sum                    # {"rev":1,"paths":["demo.c"],"sites":2,"source":"syntax"}

nextagent apply - <<'EOF'                   # several edits, one revision, no quoting
@@@ replace demo.c
return a + b;
@@@ with
return a + b + 0;
@@@ insert demo.c before-symbol main
/* entry point */
EOF

nextagent sub 'return sum' 'return -sum' demo.c    # one-liner
nextagent undo                                     # revision 4 = the inverse of revision 3
```

Every command that changes text writes the result to disk at once and prints a few bytes. `--brief` (or
`NEXTAGENT_BRIEF=1`) prints compact text instead of JSON, which costs fewer tokens. `--root DIR` points at another
project. Text that contains a backslash can be hard to pass through some shells (they halve `\\`): write `@BS@` for a
single backslash that must arrive; it is replaced in every argument and in `apply` scripts (not in `--new-file` files).

## Commands

`nextagent --help` lists them; a wrong command line prints only that command's synopsis.

| Purpose | Commands |
|---|---|
| **Read** | `view PATH [--lines A-B \| --symbol NAME] [--raw]`, `outline PATH [--nested]`, `select PATH NAME` |
| **Search** | `search QUERY` (text), `find QUERY` (symbol names), `locate QUERY` (by meaning), `context QUERY --budget N` (code pack for a question), `callers NAME`, `callees NAME` |
| **Edit, many places** | `apply [FILE\|-]` (script: `@@@ replace / lines / symbol / insert / create`), `patch [FILE\|-]` (JSON) |
| **Edit, small** | `sub OLD NEW PATH...`, `line PATH A[-B] TEXT`, `del PATH A[-B]`, `ins PATH N TEXT`, `append PATH TEXT` |
| **Edit, one place** | `replace PATH --old S --new S`, `replace PATH --lines A-B --new-file F`, `replace-symbol PATH NAME --new-file F`, `insert PATH --before-symbol NAME ...`, `create PATH --new-file F`, `edit` (codepoint offsets) |
| **Refactor** | `rename OLD NEW`, `references NAME`, `add-param FUNC "int x" --default 0`, `add-include TARGET HEADER` |
| **History** | `history`, `diff [REV]`, `undo [--rev N]`, `redo` |
| **Files changed outside** | `sync [PATH...]`, `merge [PATH...] [--ours\|--theirs] [--dry-run]` |
| **Servers** | `serve`, `mcp` (with `--clangd` or `--lsp`) |

Prefer symbol or text anchors (`before-symbol`, `append`, `sub`) to line numbers: a number taken from an earlier read
drifts as soon as anything above it changes. Details, error messages and the JSON forms are in
[docs/AGENT_GUIDE.md](docs/AGENT_GUIDE.md) and in the skill's `reference.md`.

## Languages and language servers

C and C++ work out of the box (a built-in tolerant scanner). Other languages use a language server, started on first
use and found on `PATH`:

| Languages | Server (first one found) |
|---|---|
| C, C++ | `clangd` (long-lived modes; one-shot CLI calls use the scanner) |
| Python | `pyright-langserver`, `basedpyright-langserver`, `pylsp` |
| Rust | `rust-analyzer` |
| Go | `gopls` |
| TypeScript, JavaScript | `typescript-language-server` |
| Zig, Lua | `zls`, `lua-language-server` |

One-shot CLI commands that address symbols or refactor (`outline`, `select`, `view`, `replace-symbol`, `insert`,
`apply`, `patch`, `replace`, `rename`, `references`, `add-param`) use these servers for non-C files;
`NEXTAGENT_NO_LSP=1` turns that off. Index-wide commands (`find`, `locate`, `context`, `callers`) stay on the cheap path.
`serve --lsp` and `mcp --lsp` use servers for everything, including `find_symbols` and the diagnostic gate.

**Verified live** (`tests/lspcheck.py`: outline, find, references, rename, in both modes): clangd, pyright and
typescript-language-server. rust-analyzer, gopls, zls and lua-language-server are wired the same way but have not been
tried here. A TypeScript project needs `typescript` 5.x in its `node_modules` (the server refuses TypeScript 7).

Add or override servers in `<root>/.aed/lsp.json`:

```json
{"servers": [{"name": "pyright", "command": "node", "args": "/path/to/pyright/langserver.index.js --stdio", "extensions": [".py"]}]}
```

On Windows an npm-installed server is a `.cmd` shim that cannot be spawned directly: start it as
`node <js entry> --stdio`, or wrap it as `"command": "cmd", "args": "/c typescript-language-server --stdio"`.

## Search by meaning

`locate "how are edits flushed"` ranks code chunks (symbols and 40-line windows, with their doc comments) by BM25 on the
text and on the names, by embeddings learned from your own code, and by phrase matches. The index is cached in
`.aed/index.cache`, so after the first call a 1500-file workspace answers in under half a second (5.5 s cold).

An embedding model sharpens it for questions that share no words with the code. Create `.aed/embed.json`:

```
{"url": "http://127.0.0.1:11434/v1/embeddings", "model": "nomic-embed-text"}    # Ollama, llama.cpp server, LM Studio, vLLM
{"command": "my-embedder", "args": "--fast"}                                     # any program: {"input":[...]} -> {"embeddings":[[...]]}
```

and run `nextagent locate "question" --embed 100000` once to embed the workspace (cached in `.aed/embed.cache`).
Plain HTTP only (no DNS, no TLS): put a local proxy in front of a remote or https service, or use `command`.
Optional keys: `doc_prefix`, `query_prefix` (set to `search_document: ` / `search_query: ` for nomic models by default),
`batch`, `timeout_ms`, `max_chars`, `api_key_env`.

Measured on 28 questions about QuickJS with one known answer each ([docs/BENCHMARK.md](docs/BENCHMARK.md)): the right
function is in the top ten in 13 cases without a model and 18 with `nomic-embed-text`, and first in 2 and 5. Treat
`locate` as a way to narrow the search, not as an oracle.

## Use it from an AI tool

- **Claude Code**: install the skill (above). The model then reaches for `apply`, `view --symbol` and `locate` by itself.
- **Any agent with a shell tool**: put the CLI cheat sheet of [docs/AGENT_GUIDE.md](docs/AGENT_GUIDE.md) (section 12) in
  its instructions.
- **MCP**: `nextagent --root DIR mcp [--clangd | --lsp]` speaks the Model Context Protocol over stdio (21 tools: `view`,
  `outline`, `select`, `replace_symbol`, `replace_text`, `patch`, `edit`, `references`, `rename`, `locate`, `context`, ...).

  ```json
  { "mcpServers": { "nextagent": { "command": "nextagent", "args": ["--root", "/path/to/project", "mcp", "--lsp"] } } }
  ```
- **JSON-RPC**: `nextagent --root DIR serve` reads one JSON object per line and writes one reply per line (not LSP
  Content-Length framing); the methods are the MCP tools. Errors are `-32602` with a message such as `stale_rev`,
  `symbol_not_found` or `disk_conflict`.

## Safety and storage

- **Transactions.** A batch touching many files commits as one revision or not at all. `--if-rev N` rejects a write if
  the workspace moved on since you looked (`stale_rev`).
- **History.** Append-only: `undo` of the head or of any revision (rebased over later edits, `undo_conflict` on overlap)
  is a new revision, and `redo` undoes the undo.
- **Outside changes.** If a person, git or a formatter edits a file between calls, the next call merges it three-way with
  the journal (changes on touching lines conflict like in git) instead of failing; `nextagent merge` does it explicitly.
- **Paths.** `..` escapes, `.git` and `.aed` components, drive/stream (`:`) components and symlink or junction targets
  are rejected. CRLF is preserved exactly.
- **Workspace files.** State lives in `<root>/.aed/`: `journal.aedj` (checksummed records, torn tails detected and
  discarded, an exclusive lock so several processes can share a workspace), `index.cache`, `embed.cache`, `lsp.json`,
  `embed.json`. Only the first holds anything you cannot rebuild.

### clangd diagnostic gate

With `serve --clangd` / `mcp --clangd`, every `edit`, `replace_symbol`, `undo` and `redo` of C/C++ is vetted before it
is committed: the changed files and the files that `#include` them are diagnosed by clangd before and after the change,
and any *added* warning or error rejects the edit (`diagnostics_regression`). Reports must carry the expected document
version, otherwise the gate fails closed; the compilation database is hashed again inside the write lock. Nothing is
written on rejection. See [docs/AGENT_GUIDE.md](docs/AGENT_GUIDE.md) section 8 for the exact rules and their limits.

## Build

Needs clang, lld, llvm, ninja and Python 3 (the libc23 it links against is vendored in `vendor/libc23`):

```sh
python build.py test --target windows        # or --target linux
python tests/parity.py build/linux/nextagent  # output must equal tests/parity.golden
python skill/package.py build/skill           # the Claude Code skill package
```

CI builds, tests and runs the parity check on both targets for every push, and publishes `windows-x64.zip`,
`linux-x64.tar.gz` and the skill package on every `v*` tag. Legacy Windows scripts (`build.ps1`, `build.sh`) remain for
the nextc compiler. The live-clangd tests are skipped when `clangd` is not on `PATH`.

## Source layout

```
src/util.*      buffers, errors, UTF-8, BLAKE2b, path rules          src/bin.h      binary cache encoding
src/json.*      JSON DOM and writer                                  src/merge.*    three-way merge
src/platform*   files, locks, processes, sockets (Windows / POSIX)   src/index.*    workspace index and its cache
src/core.*      snapshots and changesets                             src/hybrid.c   locate: BM25 + embeddings
src/store.*     journal, history, flush, merge of outside changes    src/embed.*    external embedding model client
src/c23.*       C23 scanner                                          src/semantic.* references, rename, call graph
src/lsp_*.c     LSP client, hub, diagnostics gate                    src/refactor.* token-based C refactors
src/rpc.*       JSON-RPC and MCP                                     src/main.c     CLI
tests/          tests.c, parity.py, lspcheck.py, locate_eval.py, bench.py          skill/   the Claude Code skill
```

## Limitations

- Not implemented: branches of the history, a file-watcher thread (outside changes are found by size and mtime on the
  next call), an async writer, a socket transport.
- The C scanner is tolerant rather than a full parser; exotic declarators may be misreported, and `select` is syntax
  only. `add-param` and `add-include` are C-only and token-based.
- The clangd gate misses a semantic change of a header (for example a signature) in an *unchanged* dependent; macro
  includes, conditional compilation, system include paths and generated headers are not followed.
- `locate` skips files over 8 MB and reads only UTF-8. The embedding client speaks plain HTTP only.
- Language servers other than clangd, pyright and typescript-language-server are untested here.
- State from 0.19 and earlier (`.aed/state.sqlite3`) is not read; run `sync` with the old version first.

See [CHANGELOG.md](CHANGELOG.md), [docs/AGENT_GUIDE.md](docs/AGENT_GUIDE.md), [docs/BENCHMARK.md](docs/BENCHMARK.md) and
[docs/agent-plan.md](docs/agent-plan.md).

## License

MIT, see [LICENSE](LICENSE).
