# NextAgent

[![CI](https://github.com/tozhievr/next-agent/actions/workflows/ci.yml/badge.svg)](https://github.com/tozhievr/next-agent/actions/workflows/ci.yml)

**A headless, transactional code editor for AI coding agents, with the search and navigation around it.**
The language engine is linked into the executable; Windows and Linux distributions include 30 native language plugins.

A coding agent normally reads whole files, counts line numbers and sends one edit per place. NextAgent lets it:

- **read less**: one function (`view --symbol`), a ranked answer to a question (`locate`, `context`), the callers of a
  function (`callers`);
- **change many places in one call**: a plain-text script (`apply`), one-liners (`sub`, `line`, `append`), or a rename
  across the workspace (`rename`), each one revision;
- **undo anything**: every change is an entry of an append-only, checksummed history (`undo`, `redo`, `history`);
- **share the files with people and git**: changes made outside nextagent are merged (three-way), not overwritten.

It works from a shell tool (CLI), as an MCP server, or as a JSON-RPC server, and ships a Claude Code skill.

> **Version 0.44.0, development tree.** C++26, Clang 23.1.3 and CMake 4.5.0-rc1.
> Native next-langserver engine integration; no external language-server processes.

## Contents

[What it saves](#what-it-saves) · [Install](#install) · [Quick start](#quick-start) · [Commands](#commands) ·
[Languages](#languages-and-language-servers) · [Search](#search-by-meaning) · [Use it from an AI tool](#use-it-from-an-ai-tool) ·
[Safety and storage](#safety-and-storage) · [Diagnostic gate](#diagnostic-gate) · [Build](#build) ·
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
| **Servers** | `serve`, `mcp` (native modules enabled automatically) |

Prefer symbol or text anchors (`before-symbol`, `append`, `sub`) to line numbers: a number taken from an earlier read
drifts as soon as anything above it changes. Details, error messages and the JSON forms are in
[docs/AGENT_GUIDE.md](docs/AGENT_GUIDE.md) and in the skill's `reference.md`.

## Languages and language servers

NextAgent links the `next-langserver` host and engine libraries directly into its
C++26 core. `LanguageRegistry` chooses a module from the requested file extension
and loads only the required native plugin (DLL on Windows, .so on Linux). No LSP Hub, `lxserver.exe`, Node.js, or
external language server is involved.

Distributions contain `nextagent.exe` (Windows) or `nextagent` (Linux), plus
`plugins/` with all 30 native libraries, manifests, and query resources. `outline file.py`, `select file.ts name`,
plain `serve`, and plain `mcp` use the engine automatically. `language_modules`
reports the registered modules and their lazy-load state (`lsp_servers` remains
as a JSON-RPC compatibility alias).

Sessions retain persistent syntax trees. Text changes become minimal byte edits
at UTF-8 boundaries, followed by incremental Rope/ChangeSet reparsing. Native
symbols are converted to NextAgent codepoint offsets; diagnostics retain document
versions. C/C++ also retain the existing scanner's richer declaration ranges and
syntax refactorings, after the native module parses the file.

`NEXTAGENT_PLUGIN_DIR` selects a different plugin directory.
`NEXTAGENT_NO_LSP=1` is a legacy switch that disables automatic language analysis.
`NEXTAGENT_LANGSERVER_DIR` remains compatible and selects its `plugins/` subdirectory.
Workspace `.aed/lsp.json` subprocess configuration is no longer used.

Current modules provide syntax diagnostics and document structure; semantic
references/rename for other languages still require capabilities that the engine
does not yet implement. Windows x64 and Linux x64 bundles both include all 30
modules. Linux modules are loaded with `dlopen`/`dlsym` and released with `dlclose`.

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
- **MCP**: `nextagent --root DIR mcp [--diagnostics]` speaks the Model Context Protocol over stdio (23 tools: `view`,
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
  discarded, an exclusive lock so several processes can share a workspace), `index.cache`, `embed.cache`, `embed.json`. Only the first holds anything you cannot rebuild.

### Diagnostic gate

`serve --diagnostics` and `mcp --diagnostics` compare native syntax diagnostics
before and after edits and reject added warnings/errors. The gate requires fresh
versioned reports and leaves the journal and disk unchanged on rejection. This is
syntax validation; it does not provide compiler-level semantic validation.
The historical `--lsp` flag aliases `--diagnostics`.

## Build

Use Clang/Clang++ 23.1.3 or newer, CMake 4.5.0-rc1 or newer, Ninja, and a platform
C++ runtime/SDK (MSVC + Windows SDK on Windows). All application and test sources
compile as standard C++26 with `-pedantic-errors`. The `next-langserver` host and
engine are statically linked; plugin DLLs are loaded inside the same process.

```sh
cmake -S . -B build/windows -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release
cmake --build build/windows
ctest --test-dir build/windows --output-on-failure
python tests/bundled_lsp.py build/windows/nextagent.exe
python tests/parity.py build/windows/nextagent.exe
python package.py
```

`python build.py test`, `./build.ps1 test`, and `./build.sh test` are convenience
frontends for the same CMake build. CPack creates the complete distribution;
`package.py` also writes its SHA-256 checksum. No vendored C runtime is required.
The upstream engine/host sources and plugin snapshot live under
`vendor/next-langserver`; the standalone LSP frontend is not built or packaged.

## Source layout

```
src/util.*      buffers, errors, UTF-8, BLAKE2b, path rules          src/bin.h      binary cache encoding
src/json.*      JSON DOM and writer                                  src/merge.*    three-way merge
src/platform*   files, locks, processes, sockets (Windows / POSIX)   src/index.*    workspace index and its cache
src/core.*      snapshots and changesets                             src/hybrid.cpp   locate: BM25 + embeddings
src/store.*     journal, history, flush, merge of outside changes    src/embed.*    external embedding model client
src/c23.*       C23 scanner                                          src/semantic.* references, rename, call graph
src/language_engine.cpp  native language sessions and plugin registry                    src/refactor.* token-based C refactors
src/rpc.*       JSON-RPC and MCP                                     src/main.cpp     CLI
tests/          tests.cpp, parity.py, bundled_lsp.py, locate_eval.py, bench.py          skill/   the Claude Code skill
```

## Limitations

- Not implemented: branches of the history, a file-watcher thread (outside changes are found by size and mtime on the
  next call), an async writer, a socket transport.
- The C scanner is tolerant rather than a full parser; exotic declarators may be misreported, and `select` is syntax
  only. `add-param` and `add-include` are C-only and token-based.
- Native diagnostics validate syntax. Compiler semantic errors, conditional compilation and generated headers need a separate compiler check.
- `locate` skips files over 8 MB and reads only UTF-8. The embedding client speaks plain HTTP only.
- The bundled next-langserver provides syntax analysis, not compiler semantic analysis.
- State from 0.19 and earlier (`.aed/state.sqlite3`) is not read; run `sync` with the old version first.

See [CHANGELOG.md](CHANGELOG.md), [docs/AGENT_GUIDE.md](docs/AGENT_GUIDE.md), [docs/BENCHMARK.md](docs/BENCHMARK.md) and
[docs/agent-plan.md](docs/agent-plan.md).

## License

MIT, see [LICENSE](LICENSE).

Build a Windows archive with `python build.py test` followed by `python package.py`.
Verify the runtime with `python tests/bundled_lsp.py build/windows/nextagent.exe`.

### Linux / WSL2 build

The Linux bundle is verified in WSL2 Ubuntu 26.04 with Clang 23.1.6 and CMake
4.5.0-rc1. It contains native ELF modules, not Windows DLLs. The executable uses
the system glibc and libstdc++ runtime.

```sh
CLANG=clang-23 CXX=clang++-23 python3 build.py test --target linux
python3 tests/bundled_lsp.py build/linux/nextagent
python3 tests/parity.py build/linux/nextagent
python3 package.py
python3 tests/package_smoke.py dist/nextagent-0.44.0-linux-x64.tar.gz
```

If the required CMake is not installed, `python3 tools/setup_cmake.py` downloads
the official binary into `build/toolchain` and prints its `bin` directory; add
that directory to `PATH`. CI uses the same setup helper. Both platform plugin
snapshots are included in the repository. To regenerate a snapshot from the
sibling grammar projects, run `python3 tools/build_plugins.py --source ../next-langserver --cc clang-23`.

## Harness

The desktop Harness combines next-agent, the embedded next-langserver, and the
Arcade UI library from `arcade_ui_standalone`. Run `nextagent-harness.exe` on Windows,
choose a workspace and configure an OpenRouter key and model. Linux includes
`nextagent-harness-cli`. See [Harness setup and verification](docs/HARNESS.md).
