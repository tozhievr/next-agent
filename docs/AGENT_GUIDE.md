# NextAgent — guide for AI models

This document tells an AI model (or an agent harness) how to use NextAgent correctly. It is written to be pasted into a system prompt or a tool description. Everything here describes behaviour that is implemented and tested in 0.20.1; things that do **not** exist are listed at the end.

## 1. What it is, in one paragraph

NextAgent is a transactional editor that sits between you and the files of one workspace (`--root`). You read documents, make edits addressed by **Unicode codepoint offsets** or by **C symbol name**, and each edit becomes an atomic, numbered **revision**. Revisions live in a journal (`.aed/journal.aedj`), *not* in your files. Your files on disk change only when you call **`sync`**. Because of this you can edit, check, undo and redo freely, and nothing is half-applied.

## 2. Mental model (read this first)

1. **Workspace revision (`rev`)**: a single integer for the whole workspace, `0` when empty. Every successful `edit`, `replace_symbol`, `undo` or `redo` creates revision `head + 1`. `rev` is global, not per file.
2. **Virtual view**: `view` returns the text *including unsynced revisions*. Disk may be behind.
3. **Optimistic concurrency**: pass `if_rev` = the revision you based your decision on. If someone else committed in between, you get `stale_rev` and nothing changes. Re-read, re-plan, retry.
4. **Offsets are codepoints**, zero-based, end-exclusive: `[start, end)`. Not bytes, not UTF-16 units, not lines. An emoji is one codepoint. Newline is one codepoint (`\n`); CRLF files contain `\r` and `\n` as two codepoints and are preserved exactly.
5. **Everything in one batch refers to the original text.** Do not shift offsets for earlier ops in the same request.
6. **Undo is a new revision** (append-only history). `undo` never deletes anything; it commits the inverse as `rev N+1`. `redo` is an undo of that undo.

## 3. Ways to talk to it

| Mode | Command | Use when |
| --- | --- | --- |
| MCP (stdio) | `nextagent --root DIR mcp [--clangd]` | Your harness supports MCP. Tools are `view outline select replace_symbol edit history sync`. |
| JSON-RPC (stdio) | `nextagent --root DIR serve [--clangd]` | Long-lived process, one JSON object per line in, one per line out. |
| CLI | `nextagent --root DIR <cmd>` | One-shot calls from a shell tool. State persists between calls via the journal. |

`--clangd[=path]` turns on the diagnostic gate (section 8). The examples below use JSON-RPC; MCP tools take the same `params` as `arguments` and return the JSON result as text, with `isError: true` on failure.

## 4. Methods

All paths are workspace-relative, `/`-separated. Absolute paths, `..` escapes, `.git`/`.aed` components and symlink targets are rejected.

### `open`
No params → `{"name":"NextAgent","version":"0.20.1","rev":<head>}`. Call once to learn the current head.

### `view`
`{"target":"src/m.c"}` → `{"rev":0,"path":"src/m.c","text":"..."}`.
- Returns the text with unsynced revisions applied.
- A file that does not exist reads as empty text — that is how you **create** files (edit `0..0` of a new path).
- `rev` in the reply is informational; use the workspace head (`open` / result of the last edit) for `if_rev`.

### `outline`
`{"target":"src/m.c","nested":false}` → `{"symbols":[{name,kind,start,end,line,column,scope}]}`.
- Default: top-level function definitions (`function`), typedefs (`typedef`) and declarations/variables (`declaration`).
- `nested:true` additionally lists tagged `struct`/`union`/`enum` definitions and their `field`s, with `scope` like `Outer::Inner`.
- `start`/`end` are codepoint offsets of the whole declaration; `line` is 1-based, `column` 0-based codepoints.
- Syntax only: it does not resolve names across files or macros.

### `select`
`{"target":"src/m.c","name":"g","kind":"function","scope":""}` → one symbol (same shape as above).
- `kind` and `scope` are optional filters. `select` always searches nested symbols.
- Errors: `symbol_not_found: g`, `ambiguous_symbol: g (2 matches)` → add `kind`/`scope`, or fall back to `edit` with offsets.

### `replace_symbol`
`{"target","name","replacement","if_rev","kind?","scope?","label?"}` → `{"rev":2,"paths":["src/m.c"]}`.
- Replaces the **whole declaration** (e.g. the entire function including its body) with `replacement`. Include the full new declaration text, without a trailing newline unless the original range included one.
- `if_rev` is **required and must equal the current head**, otherwise `stale_rev`.

### `edit`
```json
{"ops":[{"path":"src/m.c","start":4,"end":5,"text":"ff"}, {"path":"src/m.h","start":0,"end":0,"text":"// hi\n"}],
 "if_rev":2, "label":"rename f"}
```
→ `{"rev":3,"paths":["src/m.c","src/m.h"]}`.
- All ops across all files commit as one revision, or none do.
- Per file the ranges must be sorted-compatible and **non-overlapping** (they are sorted for you). Overlap → `edits must be sorted and non-overlapping`. Out-of-range → error, nothing applied.
- Insert = `start == end`. Delete = `text == ""`. Replace = both.
- `if_rev` is optional but you should always send it. `label` is free text shown in history.

### `history`
- `{"op":"log","n":20}` → array, newest first: `[{"rev":2,"parent":1,"label":"replace_symbol","ts":...}]`.
- `{"op":"undo"}` → undo the head. `{"op":"undo","rev":1}` → undo revision 1 **even if later revisions exist**; its change is rebased over them. If a later revision touched the same region you get `undo_conflict: revision 1 overlaps later revision 2 in src/m.c` and nothing changes.
- `{"op":"redo"}` → re-apply the most recently undone head revision. Only valid immediately after an undo (the head must be an "undo revision N").
- Each undo/redo returns `{"rev","paths"}` like an edit.

### `sync`
`{"paths":["src/m.c"]}` (omit `paths` for everything) → `{"ok":true,"written":["src/m.c"]}`.
- Writes the virtual text to disk atomically (temp file + rename), creating directories as needed.
- Fails with `disk_conflict` if the file on disk changed since NextAgent last saw it (someone else edited it). It never overwrites silently. Resolve by inspecting the file, then deciding what to do — there is no automatic merge.

## 5. Recommended workflow

1. `open` → remember `rev`.
2. `outline` (or `select`) the file instead of `view`ing all of it. Use `view` only for the part you must read; the text is the whole file, so prefer `select` to get exact offsets and then slice what you need.
3. Make the change:
   - Replacing a whole function/type/variable → `replace_symbol` (no offset math).
   - Anything else → `edit` with offsets taken from `select`/`outline`/your own count over the text returned by `view`.
   - Cross-file refactor → one `edit` with ops in several files so it is atomic.
4. Read the returned `rev`; use it as `if_rev` for the next call.
5. Verify with `view`/`select` (cheap), run your build/tests against disk after `sync`.
6. `sync` when the state is good. If a check fails, `history undo` instead of hand-reverting.

Keep `if_rev` chaining strict: take it from the previous reply, not from memory.

## 6. Computing offsets safely

- Take offsets from tool output (`select`/`outline` give `start`/`end`) whenever possible.
- If you count yourself, count **codepoints** in the string returned by `view`. A reliable pattern: `select` the symbol to get `start`, then compute within the symbol text.
- To change a substring: find its codepoint index `i` in the file text; `start = i`, `end = i + codepoints(old)`.
- Prefer small precise ranges to rewriting large regions: smaller ops overlap less and are less likely to conflict when undoing old revisions.
- After a batch edit, offsets of everything after the change have moved; re-`select` before the next edit.

## 7. Error reference

Errors arrive as JSON-RPC error `-32602` with a `message` (MCP: `isError:true`, message as text). Other codes: `-32700` bad JSON, `-32600` bad request, `-32601` unknown method.

| Message starts with | Meaning | What to do |
| --- | --- | --- |
| `missing parameter: X` / `invalid parameter: X ...` | Wrong or absent param | Fix the call. |
| `stale_rev: expected A, current B` | Head moved | `open`/`view` again, redo your plan with `if_rev=B`. |
| `stale_snapshot: path` | The file changed while validation ran | Retry. |
| `symbol_not_found` | No such symbol | `outline` the file; check `kind`/`scope`/spelling. |
| `ambiguous_symbol` | Several matches | Add `kind`/`scope` or use offsets. |
| `edits must be sorted and non-overlapping` | Overlapping ops | Merge them into one op. |
| `undo_conflict` | Old revision overlaps later work | Undo the later revisions first, or fix forward with a new edit. |
| `disk_conflict` | File changed outside NextAgent | Read the disk file, decide manually, then re-`edit`/`sync`. |
| `diagnostics_regression: path: severity N: msg` | clangd gate rejected the edit | Fix the code so it does not add that diagnostic. |
| `diagnostics_unavailable_or_stale` | clangd produced no fresh report | Retry; check clangd runs and the file compiles standalone. |
| `stale_validation` | Build files changed during validation | Retry. |
| `path ...` errors | Unsafe or invalid path | Use a plain relative path inside the root. |

A failed call never partially applies. It is always safe to retry after fixing the cause.

## 8. The clangd diagnostic gate (`--clangd`)

When enabled, `edit`, `replace_symbol`, `undo` and `redo` are checked **before** committing:

- Changed `.c/.h/.cc/.cpp/.cxx/.hh/.hpp/.hxx` files and the translation units that `#include "..."` them (transitively) are compared before vs after.
- Any **new** diagnostic with severity ≤ 2 (errors and warnings) rejects the whole batch with `diagnostics_regression`. Pre-existing diagnostics are ignored; removing diagnostics is fine.
- Include directories come from `compile_commands.json` in the root or `build/`. Without it clangd uses default flags, so project headers outside the file's directory may produce errors — provide a compilation database.
- Non-C files (e.g. `.md`, `.txt`) bypass the gate.

Limits you must know: it does not re-diagnose unchanged files for *semantic* header changes (a changed function signature will not flag callers); it follows only literal quoted includes. Treat a passing gate as "no new parse/semantic errors in the edited files", not as "the project builds". Always run the real build.

## 9. Concurrency and multiple agents

Several processes may use the same workspace: writers serialize on a lock and see each other's revisions. Use `if_rev` so you cannot overwrite someone else's work unknowingly. `sync` refuses to clobber files modified outside NextAgent.

## 10. Do and don't

Do:
- Send `if_rev` on every write and chain it from the last reply.
- Prefer `replace_symbol` for whole-function rewrites and `select` for locating code.
- Batch related multi-file changes into one `edit`.
- Use `history undo` to revert; use `label` to make history readable.
- `sync` before running tools that read the disk (compiler, tests, formatters).

Don't:
- Don't assume the disk equals the view until after `sync`.
- Don't use byte offsets, UTF-16 offsets or line numbers as offsets.
- Don't send overlapping ranges in one request.
- Don't edit the same file with other tools between your `edit` and `sync` unless you accept a possible `disk_conflict`.
- Don't rely on `outline` for macro-generated code or for symbols in other files.
- Don't parse the history `ts` as anything but a Unix timestamp.

## 11. Worked example (JSON-RPC)

Task: rename function `f` to `ff` in `src/m.c` and update the prototype in `src/m.h`, then verify and save.

```json
{"jsonrpc":"2.0","id":1,"method":"open"}
→ {"result":{"name":"NextAgent","version":"0.20.1","rev":3}}

{"jsonrpc":"2.0","id":2,"method":"select","params":{"target":"src/m.c","name":"f","kind":"function"}}
→ {"result":{"name":"f","kind":"function","start":0,"end":25,"line":1,"column":0,"scope":""}}
```
`int f(void)…` — the name starts at codepoint 4 (after `int `), length 1:

```json
{"jsonrpc":"2.0","id":3,"method":"edit","params":{"if_rev":3,"label":"rename f->ff","ops":[
  {"path":"src/m.c","start":4,"end":5,"text":"ff"},
  {"path":"src/m.h","start":4,"end":5,"text":"ff"}]}}
→ {"result":{"rev":4,"paths":["src/m.c","src/m.h"]}}

{"jsonrpc":"2.0","id":4,"method":"view","params":{"target":"src/m.c"}}
→ {"result":{"rev":0,"path":"src/m.c","text":"int ff(void) { return 1; }\n..."}}

{"jsonrpc":"2.0","id":5,"method":"sync"}
→ {"result":{"ok":true,"written":["src/m.c","src/m.h"]}}
```
Run the build. If it fails and you want to back out: `{"method":"history","params":{"op":"undo"}}` then `sync` again.

## 12. CLI cheat sheet (one-shot)

```
nextagent --root DIR view PATH
nextagent --root DIR outline PATH [--nested]
nextagent --root DIR select PATH NAME [--kind K] [--scope S]
echo '{"if_rev":N,"ops":[...]}' | nextagent --root DIR edit        # or --request FILE
nextagent --root DIR history [--limit N]
nextagent --root DIR undo [--rev N]
nextagent --root DIR redo
nextagent --root DIR sync [PATH...]
```
Output is one JSON document on stdout; errors go to stderr with exit code 1. The CLI has no `replace_symbol`; use `serve`/`mcp` for it, or `select` + `edit`.

## 13. What does not exist

Do not call or promise: branches, rename/find-references/add-parameter refactorings, three-way merge, file watching, socket transport, automatic formatting, symbol resolution across files, or any language other than C/C++ for `outline`/`select`/`replace_symbol` (plain `view`/`edit`/`history`/`sync` work on any UTF-8 text file; invalid UTF-8 files are rejected).

## 14. Suggested system-prompt snippet

> You edit code through NextAgent. Address text by Unicode codepoint offsets `[start,end)` or by C symbol name. Always call `open` first, pass `if_rev` from the last reply on every write, prefer `select`/`outline` over reading whole files and `replace_symbol` for rewriting a whole function. Batch multi-file changes in one `edit`. The view includes unsynced revisions; call `sync` before running builds or tests. On `stale_rev` re-read and retry; on `diagnostics_regression` fix the code; revert with `history undo`. Never use byte offsets or line numbers.
