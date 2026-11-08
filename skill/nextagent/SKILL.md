---
name: nextagent
description: Edit and navigate code through the `nextagent` CLI instead of reading whole files and issuing one Edit per place. Use it when a change touches several places or files (rename, add a parameter, batch of edits, insert next to a function), when you need one function without reading its file, when you are looking for code by meaning rather than by name, or when you want every change in a revision history you can undo. Requires the `nextagent` binary on PATH.
---

# NextAgent

A transactional code editor. Every command that changes text is **one revision** (undoable), is written to disk
immediately and prints a few bytes. Run it from the project root, or pass `--root DIR`. State lives in `.aed/` (keep it
out of git).

Check it is installed: `nextagent --version` (this skill needs 0.39 or newer: `apply`, `sub`/`line`/`ins`, `@BS@`).
If it is missing or older, see "Install" at the end.

Use `--brief` (or `NEXTAGENT_BRIEF=1`) for compact text output; it costs far fewer tokens than JSON.

## Read less

| Need | Command |
|---|---|
| one function or type | `nextagent view PATH --symbol NAME [--raw]` |
| a window of lines | `nextagent view PATH --lines 40-80 --raw` |
| what is in a file | `nextagent --brief outline PATH [--nested]` |
| text search | `nextagent --brief search "text" [--word] [-i] [--context 2] [--path SUBSTR]` |
| symbols by name | `nextagent --brief find QUERY [--kind function]` |
| code by meaning | `nextagent --brief locate "how are edits flushed" --limit 5` |
| everything relevant to a question, cut to a token budget | `nextagent context "question" --budget 3000` |
| who calls / what it calls | `nextagent --brief callers NAME [--depth 2]` / `callees NAME` |

Start an unfamiliar task with `context` or `locate`, then `view --symbol` the hits. Do not read whole files.

## Edit

**Several edits, or a multi-line change: one `apply` script.** One call, one revision, no JSON and no quoting:

```
nextagent apply - <<'EOF'
@@@ replace src/a.c
old text
@@@ with
new text
@@@ replace src/b.c all
oldName
@@@ with
newName
@@@ symbol src/a.c parse_config
int parse_config(const char *path) { return path != 0; }
@@@ insert src/a.c before-symbol main
/* program entry point */
@@@ lines src/c.c 10-12
replacement
lines
@@@ create src/new.c
#include "a.h"
EOF
```

Blocks: `replace PATH [all]` (old text, `@@@ with`, new text; the old text must match exactly once unless `all`),
`lines PATH A-B`, `symbol PATH NAME` (replaces the whole definition), `insert PATH
before-symbol|after-symbol NAME` or `before-line|after-line N`, `create PATH`. If any block fails nothing is applied.

**Small edits, one line each** (any number of files, still one revision):

```
nextagent sub OLD NEW a.c b.c [--all]     # literal replacement
nextagent line PATH 12 'text'             # replace line 12   (12-14 for a range)
nextagent del PATH 12-14
nextagent ins PATH 12 'text'              # insert before line 12
nextagent append PATH 'text'              # add at the end of the file
```

**Semantic changes across the workspace:** `nextagent rename OLD NEW`, `nextagent references NAME`,
`nextagent add-param FUNC "int x" --default 0`. In one-shot CLI calls C/C++ work out of the box (token-accurate
syntax mode); for other languages these three need a language-server session (see below).

## Rules that save time

1. **Edits in one script must not touch the same place.** A `symbol` replacement and an `insert` next to that same
   symbol, or two `replace` blocks that overlap, fail with `overlapping_edits`; put the extra text inside the
   replacement instead.
2. **Anchor on symbols or text, not line numbers.** A number from an earlier read drifts as soon as anything above it
   changes. Use `before-symbol`/`after-symbol`, `append`, or run `view PATH --lines A-B` right before a line edit.
3. **Backslashes.** Some shells halve `\\`. Write `@BS@` wherever a single backslash must reach the program; it is
   replaced in every argument and in `apply` scripts (not in files read with `--new-file`/`--old-file`). For long or
   awkward text put it in a file and use `--new-file F`.
4. **Prefer `apply -` with a quoted heredoc (`<<'EOF'`)** over many separate commands: fewer calls, one undo step.
5. **Every change is undoable:** `nextagent history --limit 5`, `nextagent undo`, `nextagent redo`.
6. **Do not edit tracked files with `sed -i` or other tools between nextagent calls.** If a human, git or a formatter
   changed a file anyway, nextagent merges it automatically on the next call; to do it explicitly:
   `nextagent merge [PATH] [--dry-run]`.
7. Errors are one short line naming the cause (`text_not_found`, `ambiguous_symbol`, `disk_conflict`, ...). A failed
   call changes nothing; fix the cause and repeat it.

## Other languages (language servers)

Symbols, `rename`, `references` and `add-param` for non-C languages use a language server, which runs in the
long-lived modes: `nextagent mcp --lsp` (MCP) or `nextagent serve --lsp` (JSON-RPC over stdio; methods `outline`,
`find_symbols`, `references`, `rename`, ... take the same arguments as the CLI). Servers found on PATH are used
automatically (clangd, pyright, rust-analyzer, gopls, typescript-language-server, ...). Add or override servers in
`.aed/lsp.json`:

```
{"servers":[{"name":"pyright","command":"node","args":"/path/to/pyright/langserver.index.js --stdio","extensions":[".py"]}]}
```

Verified live: clangd, pyright, typescript-language-server (needs `typescript` 5.x in the project). On Windows start
npm-installed servers as `node <js entry> --stdio`; a `.cmd` shim cannot be spawned.

## Better `locate`

`locate` works with no setup (BM25 + names + local embeddings). A real embedding model sharpens it for questions that
do not share words with the code: create `.aed/embed.json`, for example for a local Ollama with
`ollama pull nomic-embed-text`:

```
{"url":"http://127.0.0.1:11434/v1/embeddings","model":"nomic-embed-text"}
```

and run `nextagent locate "question" --embed 100000` once to embed the workspace (cached in `.aed/embed.cache`).
Plain HTTP only; for https use a local proxy or `"command"` mode.

## Install

Download `nextagent-<version>-windows-x64.zip` or `nextagent-<version>-linux-x64.tar.gz` from
https://github.com/tozhievr/next-agent/releases, or on Linux
`curl -fsSL https://raw.githubusercontent.com/tozhievr/next-agent/main/install.sh | sh`
(Windows: `install.ps1` in the repository). Put `nextagent` on PATH.

More detail: `reference.md` next to this file.
