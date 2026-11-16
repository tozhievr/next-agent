# NextAgent 0.45.1 workflow reference

The core exposes these APIs through JSON-RPC, MCP and the in-process Harness.
`nextagent --root DIR call METHOD -` reads the parameter object from stdin. The
Harness advertises the same 32 tools to OpenRouter; no extra file-access bridge is used.

## Read related code in one call

```json
{"requests":[{"target":"auth.py","symbol":"login","with_anchor":true},{"target":"auth.py","symbol":"logout"},{"target":"CMakeLists.txt","start_line":1,"end_line":35}],"budget_bytes":12000}
```

Pass this object to `read_batch`. Its budget limits the UTF-8 serialization of the
result object (protocol envelopes add overhead). Each item keeps its request index.
A partial item has `truncated:true`; the overall response reports omitted requests.
Line windows, symbols and labels can be mixed. Errors are returned per item.

Ordinary native reads reconcile external changes with the journal automatically.
Independent edits merge; overlapping edits return `external_conflict`. Read-only
runs reject reconciliation requiring changes rather than silently writing source.

## Read a symbol and get its edit anchor

Call `view`:

```json
{"target":"auth.py","symbol":"login","with_anchor":true}
```

The response includes the actual AST node kind, codepoint range, source fingerprint,
revision, text and an opaque `ast_...` anchor. Text and anchor come from one immutable
source snapshot. There is no need to calculate offsets or immediately re-read the file.

C/C++ and Python have declaration-specific extraction. Common declaration-shaped
ASTs also provide full function/class ranges for JavaScript, TypeScript, Kotlin,
Swift, Go, C#, Rust, PHP and GLSL. Coverage depends on each grammar; the regression
suite checks complete function reads in Python/TS/JS/Kotlin/Swift/Go, not all possible
language constructs.

## Give an AST node a semantic label

Bind the returned anchor with `labels`:

```json
{"action":"bind","label":"auth.login","anchor":"ast_..."}
```

Read it later with `view {"label":"auth.login"}`, or include that locator in
`read_batch`. Named labels and opaque anchors survive process restarts in the
checksummed journal. Useful names describe agent intent: `parser.entry`,
`auth.login`, `storage.retry-policy`.

This is lightweight agent semantics, not compiler symbol binding. An anchor
identifies a real native grammar node using its path, grammar kind, enclosing
symbol identity when available, and exact source fingerprint. Moving unrelated
text before it can change offsets without invalidating the anchor. Changing or
removing the node produces `stale_anchor`; indistinguishable nodes in the same
owner produce `ambiguous_anchor`. The resolver does not guess from the old offset.
Node-owned comments/trivia are included, so formatting can conservatively invalidate
an anchor. Labels do not automatically follow a file move or semantic rename.

For lower-level nodes, query `labels`:

```json
{"target":"auth.py","symbol":"login","node_kind":"function_def","limit":20}
```

`node_kind` is grammar-specific and is reported by queries; `function_def` is the
Python example. Optional `within` constrains a query to another anchor. `offset`
is a codepoint position that must lie inside the node. Symbol, kind, range and
fingerprint filters run before AST output materialization, allowing focused queries
on large files. Broad requests are bounded to 20,000 matching nodes and depth 512.

`labels {"action":"list"}` lists bindings. Resolve explicitly with
`{"action":"resolve","label":"auth.login"}`. Rebinding/removal must supply the
old `expected_anchor`; concurrent sessions cannot silently replace each other's
binding. `ast_` is reserved for opaque IDs. Read-only runs can query/resolve but
cannot bind, unbind or apply edits.

## One checked change, or a reviewable transaction

`change` reads current inputs, prepares edits and commits/flushes them in one call:

```json
{"edits":[{"label":"auth.login","new":"def login(user):\n    return user.strip()\n\n"}],"label":"Normalize login input"}
```

Edits accept an opaque `anchor`, a named AST `label`, a `target` plus `symbol`, or
unique `old` text. `position` can be `replace`, `before` or `after`. Multiple edits
within a file use one frozen before snapshot; overlaps are rejected. Raw offsets
are available only with an explicit source `expected_hash`. Existing CRLF style is
preserved for inserted LF text.

For review, send the same object to `prepare`. It does not apply source changes;
it returns an immutable plan ID and compact exact before/after hunks. Read the
proposed code without applying it:

```json
{"requests":[{"plan":"plan_...","target":"auth.py","side":"before"},{"plan":"plan_...","target":"auth.py","side":"after","start_line":1,"end_line":40}],"budget_bytes":12000}
```

Apply with `commit {"plan":"plan_..."}`. Each path's content and existence are
rechecked under the journal lock; stale plans and external disk conflicts fail
before source mutation. Unrelated revisions can coexist; optional `if_rev` enforces
an exact workspace head. Retrying the same plan is idempotent. The response separates
`committed` from `written` and reports the current revision and actual written paths.

A commit is atomic in the journal. Filesystem flushes occur per path, with directories
created first and removed last; they are not a multi-file operating-system atomic
rename. If a flush fails, the committed state remains recoverable and `written:false`
with `flush_error` prevents a false success. Fix the obstruction and retry the plan
or use `sync`. With `--no-sync`, commits intentionally remain pending on disk.

## Create, move and remove files with history

Include `files` in `prepare` or `change`:

```json
{"files":[{"op":"mkdir","path":"lib"},{"op":"create","path":"lib/new.py","text":"value = 1\n"},{"op":"move","path":"old.py","to":"lib/old.py"},{"op":"delete","path":"obsolete.py"}],"label":"Reorganize implementation"}
```

Operations work on UTF-8 documents and directories. Destination replacement is
rejected. Directory moves/deletes require `recursive:true` when nonempty. Operations
see earlier planned operations, so mkdir/create/move can be one transaction. Created
parents are included in the journal and undo/redo restores both contents and
existence. Limits are 2,000 changed paths and 8 MiB of combined before/after text.
Binary files are rejected rather than converted. Workspace escapes, reserved
`.git`/`.aed` paths and reparse points remain protected.

## Cache and capabilities

`capabilities {}` reports version, real module loaded/available states, supported
operations and grammar limitations. Syntax diagnostics/outline/AST labels do not
imply compiler-level references or rename. Mermaid supports ER/class diagrams,
not flowcharts.

`warm {"targets":["auth.py","parser.cpp"]}` preparses selected files into the
current server's native sessions. Derived AST/outline data is cached under
`.aed/native-cache`, keyed by exact source, plugin binary and linked executable
fingerprints. Invalid/corrupt caches fall back to parsing. A restart can reuse derived
results without pretending that a parser module was loaded. Cache counters state
that they are workspace language-engine totals. An entirely new source still needs
its first parse; this remains more expensive than a cache hit.

## Know what a successful check actually covered

`run_check` supports fixed CMake build, CTest and Python unittest commands. It returns
real output, exit code, timeout/cancellation, checked revision and input fingerprints.
Optional `inputs` is a nonempty array of workspace paths. Otherwise the source-tree
scope excludes `.git`, `.aed`, build/dist, dependency directories and reparse entries;
the scope and completeness are reported. External toolchains are not fingerprinted.

Use `check_status {"check_id":"..."}` (or `{}` for latest) to compare that proof
against current source inputs and revision. Inputs changed during execution cannot
later be treated as a valid check. This is a point-in-time freshness check, not a
hermetic build or continuous filesystem watcher.

The Harness displays the checked revision and invalidates its visible proof after
subsequent model edits. Restored sessions require rechecking freshness. Long-running
checks use an independent native store connection, so they do not block UI file
reads; concurrent source edits make the result stale.

## Journal compatibility

0.45 reads existing journals and adds a format-2 marker on the first journaled action.
Metadata, file existence and directory semantics require the updated binary. Older
binaries reject upgraded journals; downgrading a live journal is unsupported.
Keep `.aed/` private to the workspace and out of git.

## Verification

`tests/agent_features.py` covers durable labels, prefix relocation, stale/ambiguous
anchors, reserved names, cross-session CAS, prepared snapshots, stale plans,
idempotency, recursive filesystem operations and undo/redo, byte budgets, MCP/CLI,
900-function AST filtering and check freshness. Harness tests additionally cover
read-only enforcement and source reads during a concurrent long-running check.


Measured latency and payload reports are included under docs/benchmarks in the distribution.

## CLI transport and compact editing

`nextagent skill [--reference] [--json]` reads the built-in skill without creating `.aed/`. `nextagent call METHOD -` accepts one JSON object on stdin. On PowerShell use a here-string piped to that command; on POSIX use a quoted heredoc. `--root DIR` belongs before the command.

`nextagent apply -` accepts blocks such as:

```text
@@@ replace src/a.cpp
old text
@@@ with
new text
@@@ symbol src/b.py login
def login(user):
    return user.strip()
@@@ create src/new.py
value = 1
```

Exact old text must match once unless the replace header ends with `all`. Edits in a transaction must not overlap; put adjacent changes inside a single replacement. `@BS@` in CLI arguments/apply scripts becomes a literal backslash; text loaded from `--new-file` is verbatim.

`nextagent history --limit 5`, `nextagent diff`, `nextagent undo`, and `nextagent redo` inspect/revert journaled changes. `nextagent sync` flushes pending state; `nextagent merge PATH --dry-run` previews reconciliation. An external conflict requires an explicit choice or a manual resolution; do not overwrite it blindly.
