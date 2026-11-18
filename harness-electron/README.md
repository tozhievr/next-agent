# NextAgent Harness 0.47

Electron + Next.js + Tailwind CSS + standard neutral shadcn/ui themes. The legacy Arcade/Win32 frontend is removed; the C++26 next-agent/next-langserver core remains embedded through Node-API in a worker thread.

## Run and build

```sh
bun install --frozen-lockfile
bun run native:build
bun run build
bun run desktop
```

For development: `bun run desktop:dev`. Requires Clang 23.1.3+, CMake 4.5, Ninja and Python. Configure the parent native build with `python build.py test` first when the compiler is not the host default. Linux also needs libcurl development headers.

The static renderer is served by Electron over a local protocol; no Next.js server runs in the production app. All workspace reads/edits, session persistence, tools and verification use the linked next-agent API. OpenRouter keys stay in the main process after saving; persistent storage requires system-backed Electron encryption.

## Installer and portable distributions

```sh
bun run dist
```

Build on the target host. Output is isolated under `release/windows/`, `release/linux/` or `release/macos/`:

| Platform | Portable | Animated installer |
|---|---|---|
| Windows x64 | `NextAgent-Harness-0.47.0-windows-x64-Portable.exe` | `setup/NextAgent-Harness-0.47.0-windows-x64-Setup.exe` |
| Linux x64 | `NextAgent-Harness-0.47.0-linux-x86_64-Portable.AppImage` | `setup/NextAgent-Harness-0.47.0-linux-x86_64-Setup.AppImage` |
| macOS | `NextAgent-Harness-0.47.0-mac-<arch>-Portable.dmg` | `setup/NextAgent-Harness-0.47.0-mac-<arch>-Setup.dmg` |

Each artifact has a SHA-256 sidecar. Setup is a custom Electron wizard with vector animation, folder selection, actual copy progress and per-file integrity checks. It stages the payload before replacing an existing marked installation; unmarked directories are protected. Raw `original-fs` APIs copy ASAR archives as binary files instead of traversing Electron's virtual archive view. Installation failures remove only staging data. Windows setup adds user shortcuts and an Apps uninstall entry; Linux adds a user application-menu entry. The app's `--uninstall` flow validates the installation marker and preserves projects and external session data.

Portable launches keep application data beside the executable/AppImage in `NextAgent Harness Data`. Installed launches use the normal per-user application-data location. Move the entire portable data folder with the executable to retain sessions; keep credentials private. On Linux, FUSE support is needed for ordinary AppImage mounting; `APPIMAGE_EXTRACT_AND_RUN=1` is available on systems that cannot mount it.

A separate [Harness release workflow](../.github/workflows/harness-release.yml) builds Windows, Linux and macOS independently of core CLI releases. A `harness-v*` tag that matches `package.json` publishes the portable and setup files on that GitHub release. The native core CLI remains independently distributed; no Win32 GUI is included in it. macOS language plugins are generated with lxgen from the pinned `tozhievr/next-langserver` grammars and compiled in that job. Packaging uses the Electron binary from `node_modules/electron/dist` and does not publish on its own.

## Instructions and provider caching

[Canonical instruction.md](../harness/instruction.md) describes tools, source access, environment boundaries, AST anchors, transactions and verification. CMake embeds it in the addon; a workspace file cannot silently replace it. Stable instruction text comes first, then workspace/OS/profile and applicable root AGENTS.md. Live timestamps/revision counters are excluded from that prefix; tool schemas have deterministic ordering.

Requests carry a persistent session identifier and cache key. Explicit text breakpoints cover reusable instructions, environment and the growing conversation tail; Claude/auto requests also opt into automatic conversation caching. Saved history remains unchanged by these transport hints. The UI reports provider-returned cache-read and cache-write tokens; it does not estimate savings or promise hits. Model support, minimum sizes, retention and write/read prices differ: [OpenRouter caching reference](https://openrouter.ai/docs/guides/best-practices/prompt-caching).

## Verification

```sh
bun run lint
bun run typecheck
bun run test:desktop
bun run build
bun run test:e2e
```

Native tests cover transactions, browsing, path rejection, history and session restore. Installer engine tests cover payload integrity, cancellation, protected destinations, rollback and managed removal. Playwright launches the actual desktop. Set `NEXTAGENT_SETUP_EXECUTABLE` to the unpacked setup executable to include a complete installer UI test; it installs the real payload into a temporary folder and skips OS registration. `NEXTAGENT_TEST_EXECUTABLE` selects a packaged desktop for UI tests. Tests use no paid OpenRouter calls.

`Проверить ядро` runs a scripted native edit in a temporary workspace; it is an offline smoke test, not a model-quality evaluation.
