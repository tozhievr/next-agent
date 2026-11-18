# NextAgent Harness

The Harness links next-agent and next-langserver into one C++26 runtime. Workspace
reads, search, navigation, edits, history and diffs call the native next-agent API
inside the process. File extensions select the appropriate bundled language module.
There is no language-server subprocess and no generic shell tool exposed to models.

## Run

Windows: launch the separate NextAgent Harness application, select a workspace, open settings and enter
an OpenRouter API key and a model ID supporting tool calls (default `openrouter/auto`).
The desktop is Electron + Next.js + Tailwind CSS + shadcn/ui. The former Arcade/Win32 frontend is removed.

The key can also be supplied as `OPENROUTER_API_KEY`. Saved Windows keys use DPAPI
for the current user. Linux uses the environment variable and ships the CLI:

```sh
export OPENROUTER_API_KEY=your-key
nextagent-harness-cli --root /path/to/project --model openrouter/auto --prompt 'Explain this project' --json
```

Both clients use OpenRouter HTTPS chat completions with streamed text and function
calls. Tool results return to the model until completion or the step limit. Cancellation
stops HTTP requests and running checks. Sessions and settings are persisted through
a separate next-agent store in the user application-data directory; `--state-dir`
selects another directory. Never commit credentials or application-state journals.

The right panel reports actual tool activity, changed files, elapsed time and provider
usage. Cost stays unknown until the provider reports it. The read-only profile denies
mutating tools and checks. It is an application policy, not an operating-system sandbox.

`run_check` only supports configured build/test commands: CMake build, CTest or Python
unittest discovery. These commands execute project code as the current user. Their
output, exit status, timeout and cancellation are returned through next-agent.

## Verification

```sh
python build.py test
python tests/bundled_lsp.py build/windows/nextagent.exe
python tests/parity.py build/windows/nextagent.exe
python package.py
python tests/package_smoke.py dist/nextagent-0.46.0-windows-x64.zip
```

Linux uses `build/linux/nextagent` and the Linux tar.gz package. Install libcurl
 development headers before configuring Linux (`libcurl4-openssl-dev` on Ubuntu).
Playwright tests exercise the actual Electron desktop, credential settings, native edits, file previews and screenshots. HTTP tests use a local mock provider:
fragmented SSE, tool calls, real Python checks, usage and rejected credentials.
`--offline` is an explicitly scripted self-test, not an AI model. Automated tests
make no paid OpenRouter calls; a real conversation requires your API key.

Protocol references: [tool calling](https://openrouter.ai/docs/guides/features/tool-calling)
and [streaming](https://openrouter.ai/docs/api/reference/streaming).

The model can use [AST labels and checked agent workflows](AGENT_WORKFLOWS.md).

## Electron desktop

The [Electron Harness](../harness-electron/README.md) uses Next.js static export, Tailwind CSS and shadcn/ui with the standard neutral palette. A context-isolated preload talks to a worker loading the native Node-API addon; next-agent and next-langserver run in that worker thread rather than CLI/language-server subprocesses. The model loop, workspace I/O, session history and checks reuse the native Harness implementation.

## Instruction and provider caching

`harness/instruction.md` is compiled into the native Harness and supplied before user messages. Runtime workspace/OS/profile and AGENTS.md follow in a separate stable block; tool schemas are sorted deterministically. Requests preserve conversation order and carry persistent session affinity. Explicit text cache boundaries cover instructions, environment and a rolling conversation tail; Claude/auto routing also gets the documented automatic cache hint. No timestamps or live revision numbers are injected into the prefix.

The desktop displays only cache reads/writes actually reported by the provider. Cache support, minimum size, expiry and pricing vary by model; a cache write can cost more than ordinary input. No guaranteed savings are claimed. Provider behavior: [OpenRouter prompt caching](https://openrouter.ai/docs/guides/best-practices/prompt-caching).
