# NextAgent Harness

The Harness links next-agent and next-langserver into one C++26 runtime. Workspace
reads, search, navigation, edits, history and diffs call the native next-agent API
inside the process. File extensions select the appropriate bundled language module.
There is no language-server subprocess and no generic shell tool exposed to models.

## Run

Windows: launch `nextagent-harness.exe`, select a workspace, open settings and enter
an OpenRouter API key and a model ID supporting tool calls (default `openrouter/auto`).
The UI uses the actual Arcade DX12/Win32 library from `D:\Projects\arcade\arcade_ui_standalone`;
its source snapshot is under `vendor/arcade_ui_standalone`.

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
python tests/package_smoke.py dist/nextagent-0.45.0-windows-x64.zip
```

Linux uses `build/linux/nextagent` and the Linux tar.gz package. Install libcurl
 development headers before configuring Linux (`libcurl4-openssl-dev` on Ubuntu).
Windows tests additionally exercise the actual GUI with WARP rendering, Unicode
composer input, a native edit and a screenshot. HTTP tests use a local mock provider:
fragmented SSE, tool calls, real Python checks, usage and rejected credentials.
`--offline` is an explicitly scripted self-test, not an AI model. Automated tests
make no paid OpenRouter calls; a real conversation requires your API key.

Protocol references: [tool calling](https://openrouter.ai/docs/guides/features/tool-calling)
and [streaming](https://openrouter.ai/docs/api/reference/streaming).

The model can use [AST labels and checked agent workflows](AGENT_WORKFLOWS.md).
