# NextAgent

[![CI](https://github.com/tozhievr/next-agent/actions/workflows/ci.yml/badge.svg)](https://github.com/tozhievr/next-agent/actions/workflows/ci.yml)

**A native workspace engine and coding Harness for AI agents.** Transactional edits, targeted reads, search, durable AST anchors and verification, backed by the in-process next-langserver engine.

Use the **CLI**, **MCP**, **JSON-RPC**, or the **desktop/CLI Harness with OpenRouter**. Harness workspace operations go through the native next-agent API. File extensions automatically select the appropriate language module.

**Development version: 0.46.0.** Windows x64 and Linux x64 distributions include 30 native modules. Application and test sources use C++26; language analysis needs no external server process.

[Install](#install) · [Quick start](#quick-start) · [Harness](#harness-with-openrouter) · [Labels](#ast-labels-and-transactions) · [Languages](#native-language-modules) · [Build](#build-and-test)

## What it does

- Read functions, types and related files in a budgeted batch.
- Attach names to real AST nodes and resolve them across process restarts.
- Preview coordinated changes, reject stale inputs and commit multiple files as one journal revision.
- Create, move and delete UTF-8 files and directories with undo/redo.
- Search text and symbols, retrieve ranked code and build context packs.
- Record build/test output and determine whether its source inputs are still current.

AST anchors provide lightweight agent semantics. They do not replace compiler symbol binding or type checking.

## Install

Download an archive from [Releases](https://github.com/tozhievr/next-agent/releases). Keep `plugins/` beside the executables: copying only `nextagent` is insufficient for language analysis. Development archives are built locally with `python package.py`; the development version above does not imply a published release tag.

| Platform | Included clients |
|---|---|
| Windows x64 | `nextagent.exe`, `nextagent-harness-cli.exe` |
| Linux x64 | `nextagent`, `nextagent-harness-cli` |

Both archives include plugins, manifests, skill and documentation. Install the latest published release on Windows:

```powershell
irm https://raw.githubusercontent.com/tozhievr/next-agent/main/install.ps1 | iex
```

On Linux x86-64:

```sh
curl -fsSL https://raw.githubusercontent.com/tozhievr/next-agent/main/install.sh | sh
```

Windows installs to `%LOCALAPPDATA%\NextAgent\bin` and adds it to the user `PATH`. Linux defaults to `~/.local/bin`; add it to `PATH` if necessary. `NEXTAGENT_DIR` overrides the destination; `NEXTAGENT_VERSION` selects a published tag.

```text
nextagent --version
nextagent skill
```

Keep workspace `.aed/` directories out of git. Version 0.45 upgrades journal format when writing journaled state. Older binaries reject upgraded journals; downgrading an active journal is unsupported.

## Quick start

Run from the project root, or place `--root DIR` before the command.

```sh
nextagent --brief outline src/parser.cpp --nested
nextagent view src/parser.cpp --symbol parse_config --raw
nextagent search "parse_config"
nextagent locate "how configuration is loaded" --limit 5
nextagent context "configuration parsing" --budget 3000
```

For an exact-text change:

```sh
nextagent sub 'return value + 1;' 'return value + 2;' src/example.cpp
nextagent diff
nextagent undo
```

Structured APIs use `nextagent call METHOD -` with one JSON object on stdin. Create a file through next-agent on POSIX:

```sh
nextagent call change - <<'JSON'
{"files":[{"op":"create","path":"demo.cpp","text":"int add(int a, int b) { return a + b; }\n"}],"label":"Create demo"}
JSON
nextagent view demo.cpp --symbol add --raw
```

PowerShell equivalent:

```powershell
'{"files":[{"op":"create","path":"demo.cpp","text":"int add(int a, int b) { return a + b; }\n"}],"label":"Create demo"}' | nextagent call change -
```

Choose either example in a fresh workspace: creating an existing destination is rejected. JSON strings escape newlines as `\n`. For multiline requests use a quoted POSIX heredoc or a PowerShell here-string piped to stdin.

## Harness with OpenRouter

The Electron desktop lives in [`harness-electron/`](harness-electron/README.md): Next.js, Tailwind CSS and standard shadcn/ui themes, with an in-process Node-API bridge to the same native core. Run `bun run native:build`, `bun run build`, then `bun run desktop` from that directory. The old Win32/Arcade frontend has been removed. Harness is distributed separately as Setup and Portable for Windows and Linux. A `harness-v*` tag publishes those packages.


The Harness links **next-agent + next-langserver** in one process. The desktop uses Electron, Next.js, Tailwind CSS and standard shadcn/ui themes. The model reads and changes the workspace through next-agent tools; no generic shell tool is exposed to it.

Run the separate NextAgent Harness application, choose a workspace, and configure an OpenRouter key and a model supporting tool calls. Saved Windows keys use DPAPI for the current user.

The CLI works on both platforms. Supply `OPENROUTER_API_KEY` in the environment:

```sh
nextagent-harness-cli --root /path/to/project --model openrouter/auto --prompt 'Explain this project' --read-only --json
```

The Harness streams responses and tool calls, preserves sessions, and reports activity, changed files, provider usage and check results. Cost is known only when the provider reports it. `--read-only` is an application policy, not an OS sandbox.

`--offline` runs a scripted transaction self-test without model requests. Automated tests use it and a local mock HTTP provider; they do not measure paid OpenRouter conversation quality. See [Harness setup](docs/HARNESS.md).

## AST labels and transactions

Call `view` to read code and receive an opaque `ast_...` anchor from the same snapshot:

```json
{"target":"auth.py","symbol":"login","with_anchor":true}
```

Bind the returned ID with `labels`:

```json
{"action":"bind","label":"auth.login","anchor":"ast_RETURNED_ID"}
```

Later, `view {"label":"auth.login"}` resolves that node. Labels persist across restarts and survive unrelated text inserted before a node. Changing its content invalidates the anchor; stale or ambiguous nodes return errors. Labels do not automatically follow file moves or renames.

Use `change` for an immediate checked transaction:

```json
{"edits":[{"label":"auth.login","new":"def login(user):\n    return user.strip()\n\n"}],"label":"Normalize login input"}
```

For review, send that object to `prepare`, inspect the diff or proposed content, then call `commit {"plan":"plan_RETURNED_ID"}`. Inputs are rechecked under the journal lock. Stale plans fail before source mutation; retrying an already committed plan is idempotent.

| Native API | Purpose |
|---|---|
| `read_batch` | Related reads with a UTF-8 byte budget and per-item errors |
| `labels` | Query AST nodes; bind, resolve and list names |
| `prepare`, `commit`, `change` | Preview or apply checked transactions |
| `capabilities`, `language_modules` | Inspect language support and load state |
| `warm` | Preparse files in the current server process |
| `run_check`, `check_status` | Execute supported checks and inspect proof freshness |

File transactions accept `create`, `mkdir`, `move` and `delete` operations in `files`. Nonempty directory operations require `recursive:true`. Limits are 2,000 changed paths and 8 MiB of combined before/after text; binary files are rejected.

Transactions are atomic **in the journal**. Disk writes happen per path, not as a multi-file OS atomic operation. Inspect `written` and `flush_error`; recover committed state with `sync` or a plan retry after fixing the obstruction.

Examples, limits and conflict handling: [agent workflows](docs/AGENT_WORKFLOWS.md).

## Native language modules

The statically linked next-langserver host selects plugins from manifests and loads them lazily: DLL on Windows, `.so` on Linux. Parsing runs inside the process, with incremental sessions, syntax diagnostics and AST-derived structure.

The bundle includes C, C++, C#, JavaScript, TypeScript, Python, Rust, Go, Swift, Kotlin, Haskell, Lean, Lua, PHP, HTML, CSS, JSON/JSON5, GLSL, NASM, Mermaid, ANTLRv4 and additional grammar/test modules: **30 total**. Coverage depends on the grammar; availability does not imply complete language semantics.

C++ routing includes `.cpp`, `.cc`, `.cxx`, `.hpp`, `.hxx`, `.hh` and module extensions **`.cppm`, `.ixx`, `.ccm`, `.cxxm`, `.c++m`, `.mpp`**.

```text
nextagent call capabilities
nextagent call language_modules
```

Derived outline/AST data is cached in `.aed/native-cache`, keyed by source, plugin and executable fingerprints. Results can survive process restarts; new source still requires its first parse.

`NEXTAGENT_PLUGIN_DIR` overrides the plugin directory. Legacy `NEXTAGENT_NO_LSP=1` disables automatic analysis. External `.aed/lsp.json` configuration is no longer used. LSP Hub, the built-in C23 scanner and libc23 dependency have been removed.

## Connect an agent

A model with a shell tool can read version-matched instructions directly from the binary without creating a workspace:

```text
nextagent skill
nextagent skill --reference
nextagent skill --json
```

The documents also ship under `skill/nextagent/`. Copy that folder into the agent's skill directory; see [skill setup](skill/README.md).

MCP configuration:

```json
{"mcpServers":{"nextagent":{"command":"nextagent","args":["--root","/path/to/project","mcp"]}}}
```

`nextagent --root DIR serve` provides JSON-RPC over stdio: one JSON object per line, without LSP Content-Length framing. MCP, JSON-RPC and the in-process Harness expose native workspace operations. `nextagent --help` lists CLI commands.

## Verification and limits

- Diagnostics validate syntax. Compiler semantics, conditional compilation, generated headers and linking need build/test checks. Syntax-based C/C++ refactoring is not compiler-bound rename.
- Anchors include exact node content and owned trivia; formatting can invalidate them. Mermaid supports ER/class diagrams, not flowcharts.
- Native reads reconcile external changes. Independent edits merge; overlapping changes need resolution. There is no continuous workspace watcher.
- `run_check` supports CMake build, CTest and Python unittest discovery. Checks execute project code as the current user. Freshness covers scoped source inputs and journal revision, not external toolchains or a hermetic build.
- Path checks reject workspace escapes, protected `.git`/`.aed` entries and reparse points. They are not an OS sandbox.
- Search ranks candidates; inspect relevant code before editing. The optional embedding client uses plain HTTP or a configured command; OpenRouter HTTPS is a separate Harness integration.

Regressions cover language routing, incremental parsing, stale anchors/plans, concurrency, undo/redo, check freshness, MCP/CLI, GUI and relocated distributions. C++ module tests cover interface, partition, implementation and import syntax.

[Measured 0.45 RPC results](docs/benchmarks/AGENT_045.md): for 100 C++ functions, a function read returned 116 bytes versus 5,131 for the file, about **44 times fewer response bytes**. Warm outline medians were 1.10 ms on Windows and 0.75 ms in WSL2; cold outlines took about 3 seconds. These local measurements do **not** establish model token savings or overall agent speedups.

[Integration reports](docs/benchmarks/README.md) and [historical modelled benchmarks](docs/BENCHMARK.md) describe their own setup and limitations.

## Build and test

Requirements: **Clang/Clang++ 23.1.3+**, **CMake 4.5** (development uses 4.5.0-rc1), Ninja and Python 3. Windows needs the MSVC runtime/Windows SDK. Linux needs libcurl development headers and the system C++ runtime. Application and test sources use C++26 with `-pedantic-errors`.

Windows, with the toolchain on `PATH`:

```powershell
python build.py test --target windows
python package.py
python tests/package_smoke.py dist/nextagent-0.46.0-windows-x64.zip
```

Linux or a WSL2 Linux shell:

```sh
CLANG=clang-23 CXX=clang++-23 python3 build.py test --target linux
python3 package.py
python3 tests/package_smoke.py dist/nextagent-0.46.0-linux-x64.tar.gz
```

On Ubuntu, install `libcurl4-openssl-dev` before configuring. `python tools/setup_cmake.py` downloads pinned CMake into `build/toolchain`; add its printed `bin` directory to `PATH`.

`build.py test` configures, builds and runs CTest. `package.py` uses CPack and writes SHA-256 files. Package smoke tests verify relocation, all modules, skill consistency, C++ module extensions and protocol parity. Electron UI is tested separately with Playwright. Linux is verified in WSL2 Ubuntu 26.04; compatibility with every distribution is not claimed.

Both platform plugin snapshots are included. To regenerate one from a sibling next-langserver checkout:

```sh
python tools/build_plugins.py --source ../next-langserver --cc clang-23
```

## Source layout and documentation

| Path | Responsibility |
|---|---|
| `src/` | Store, journal, languages, search, transactions, checks and CLI/protocol adapters |
| `harness/` | OpenRouter loop, transport, sessions, CLI and Windows UI |
| `vendor/next-langserver/` | Embedded engine/host, downstream fixes and plugins |
| `harness-electron/` | Desktop UI, animated setup, portable packaging and Node-API bridge |
| `skill/` | Canonical model instructions and skill packaging |
| `tests/`, `tools/` | Regressions, integration evaluation, builds and measurements |

- [Agent workflows and AST labels](docs/AGENT_WORKFLOWS.md)
- [Harness setup and verification](docs/HARNESS.md)
- [Model-facing skill reference](skill/nextagent/reference.md)
- [RPC measurements](docs/benchmarks/AGENT_045.md) and [integration reports](docs/benchmarks/README.md)
- [Changelog](CHANGELOG.md)

## License

[MIT](LICENSE).
