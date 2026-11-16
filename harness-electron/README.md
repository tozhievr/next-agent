# NextAgent Electron Harness

Electron + Next.js 16 + Tailwind CSS 4 + shadcn/ui (standard neutral tokens, dark/light themes).

The renderer calls a context-isolated preload API. Electron forwards a limited set of operations to a worker that loads `nextagent_desktop.node`. The addon links the existing C++26 next-agent/next-langserver/Harness core directly; it does not spawn a language server or CLI process. File reads, edits, sessions and settings use native next-agent stores. OpenRouter streaming and tool execution reuse the native Session implementation.

## Development

From this directory, with Bun, Python, CMake 4.5, Ninja and Clang 23.1.3+ installed:

```sh
bun install
bun run native:build
bun run desktop:dev
```

`native:build` configures the parent CMake build and copies the addon and all 30 platform plugins into `native/bin/win/` or `native/bin/linux/`. On Windows it downloads the Electron import library matching the locked runtime version. First configure the parent native build with `python build.py` if the required compiler is not the default. Linux additionally requires libcurl development headers.

## Production build

```sh
bun run native:build
bun run build
bun run desktop
bun run desktop:build
```

Next.js statically exports the renderer to `out/`. The production desktop serves those assets over a local custom protocol, without a Next.js server. Electron Builder creates an unpacked app under `release/` with the addon, plugins and renderer as resources.

Open a workspace, configure a tool-capable OpenRouter model and API key, then send a task. `OPENROUTER_API_KEY` is also supported. Keys are never returned to the renderer after saving; persistence requires Electron system-backed encryption. Session-only storage is used when secure storage is unavailable. Read-only mode is an application policy, not an OS sandbox.

The footer's “Проверить ядро” runs the scripted offline self-test in a temporary workspace. It creates `harness-check.txt` through a next-agent transaction and makes no model requests. It is not a model quality evaluation.

## Verification

```sh
bun run lint
bun run typecheck
bun run test:desktop
bun run build
bun run test:e2e
```

Native integration covers transactions, file browsing, path rejection, history and session restore. Playwright launches actual Electron and checks the native self-test, file preview, theme switching and screenshots. No paid OpenRouter calls are made by these tests.

Do not commit `native/bin/`, `out/`, `release/`, API keys or application-state journals. The renderer has Node integration disabled, context isolation and sandbox enabled; IPC is scoped to its trusted main frame.

Verified locally: TypeScript and lint, Windows native integration, actual Electron UI tests and the unpacked Windows application. Linux native bridge tests run under WSL2; the Linux Electron GUI is not part of that headless test.
