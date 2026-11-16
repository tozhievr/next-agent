---
name: nextagent
description: Navigate and edit a code workspace with the nextagent CLI, native AST labels and checked multi-file transactions. Use for targeted code reads, coordinated edits, file moves, undoable changes and verification through next-agent.
---

# NextAgent 0.45.1

Use `nextagent --version` to confirm the binary. `nextagent skill` returns this instruction from the binary itself; `nextagent skill --reference` returns the detailed workflow reference. Neither command opens a workspace or loads language modules. `--json` returns a versioned machine-readable document.

## Choose a small amount of relevant code

Run from the workspace root or use `nextagent --root DIR ...`. Use `outline PATH --nested`, `view PATH --symbol NAME --raw`, `search QUERY`, `find QUERY`, `locate QUERY` or `context QUERY --budget 3000` to discover code. `--brief` reduces ordinary CLI output; native API results are usually best kept as JSON.

For related reads use `nextagent call read_batch -` with a JSON parameter object on stdin:

```json
{"requests":[{"target":"auth.py","symbol":"login","with_anchor":true},{"target":"auth.py","symbol":"logout"}],"budget_bytes":12000}
```

A truncated result is partial evidence: read the omitted region before changing it. Budgets cover the UTF-8 result object, not protocol framing.

## Read, anchor and change

`nextagent call view -` with `{"target":"auth.py","symbol":"login","with_anchor":true}` returns text and an opaque `ast_...` anchor from one source snapshot. Use that returned anchor directly in `change`:

```json
{"edits":[{"anchor":"ast_RETURNED_ID","new":"def login(user):\n    return user.strip()\n\n"}],"label":"Normalize login"}
```

For durable intent, bind with `labels {"action":"bind","label":"auth.login","anchor":"ast_RETURNED_ID"}`. Later read with `view {"label":"auth.login"}` and edit with `{"edits":[{"label":"auth.login","new":"..."}]}`. The outer transaction `label` describes history; an edit's `label` locates an AST binding.

Anchors survive unrelated text inserted before a node and process restarts. Changed nodes produce `stale_anchor`; identical candidates can produce `ambiguous_anchor`. Re-read and choose the intended node; never retry with guessed offsets. Labels do not automatically follow file moves or renames. Rebinding/unbinding requires the previous `expected_anchor`.

For reviewable changes call `prepare` with the same edits/files object, inspect its diff or `read_batch` with `plan`, `target` and `side:"after"`, then `commit {"plan":"plan_RETURNED_ID"}`. A stale plan must be prepared again. Retrying the same committed plan is idempotent. A journal commit and disk flush are distinct: inspect `written` and `flush_error`; recover with `sync` or a plan retry after fixing the obstruction.

## Files, checks and capabilities

Create/move/delete directories and UTF-8 files through `change` or `prepare` using `files`. These operations share the transaction and undo history. Nonempty directory operations require `recursive:true`; binary files and workspace escapes are rejected. See the reference for examples and limits.

Use `nextagent call capabilities` before relying on a language feature. Bundled next-langserver modules load in-process by extension; external LSP executables are not required. AST nodes and syntax outlines are not compiler-level semantic references or rename. Check reported capabilities and ambiguity before a cross-workspace refactor.

Run relevant checks through `run_check` and consult `check_status` before reporting that their proof is current. A successful command does not prove later edits passed. `.aed/` holds journal and derived caches; keep it out of git. 0.45 upgrades journal format; older binaries cannot safely reopen an upgraded journal.

Use nextagent for source reads and edits in workflows that require it. Build/test tools and git can still operate normally. External source changes are reconciled on native reads; overlapping changes require conflict resolution.

## Shell transport and legacy editing

Prefer JSON on stdin for multiline text: POSIX quoted heredoc or PowerShell here-string piped to `nextagent call METHOD -`. JSON strings must escape newlines as `\n`. Do not interpolate source into shell commands. `nextagent apply -` also supports multiple exact-text/symbol edits in one revision; details are in [reference.md](reference.md).

Read [reference.md](reference.md), also available with `nextagent skill --reference`, for complete transaction examples, label queries, checks and failure recovery.
