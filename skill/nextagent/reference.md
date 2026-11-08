# NextAgent reference

Everything the CLI does, grouped by purpose. `--brief` prints compact text; global flags (`--root DIR`, `--brief`,
`--no-sync`) may appear anywhere. `nextagent --help` lists every command; a wrong command line prints only that
command's synopsis.

## Read

```
nextagent view PATH [--lines A-B | --symbol NAME [--kind K] [--scope S]] [--raw]
nextagent outline PATH [--nested]            # line kind scope::name
nextagent select PATH NAME [--kind K]        # one symbol: offsets, lines
nextagent search QUERY [--word] [-i] [--context N] [--path SUBSTR] [--limit N]      # grep
nextagent find QUERY [--kind K] [--limit N]                                           # symbols by name
nextagent locate QUERY [--path SUBSTR] [--limit N] [--embed N]                        # ranked, by meaning
nextagent context QUERY [--budget TOKENS] [--path SUBSTR]                              # best code, token-budgeted
nextagent callers NAME [--depth N] [--path P]    nextagent callees NAME [--depth N]
nextagent references NAME [--no-members] [--path P]...
```

`NEXTAGENT_TRACE=1` prints where the time of a `locate` went. The first `locate`/`search` in a large workspace builds
`.aed/index.cache`; later calls reuse it (about 10x faster).

## Edit

```
nextagent apply [FILE|-] [--label S]       # many edits, one revision (script format in SKILL.md)
nextagent sub OLD NEW PATH... [--all]      nextagent line PATH A[-B] TEXT     nextagent del PATH A[-B]
nextagent ins PATH N TEXT                  nextagent append PATH TEXT
nextagent create PATH (--new S | --new-file F)
nextagent insert PATH (--before-line N | --after-line N | --before-symbol NAME | --after-symbol NAME)
                      (--new S | --new-file F) [--kind K] [--scope S]
nextagent replace PATH (--old S | --old-file F | --lines A-B) (--new S | --new-file F) [--all] [--if-rev N]
nextagent replace-symbol PATH NAME (--new S | --new-file F) [--kind K] [--scope S]
nextagent patch [FILE|-]                   # JSON [{path, old|start_line/end_line|before_line|after_line|at_end|symbol|
                                           #        before_symbol|after_symbol|create, new, all}]
nextagent rename NAME NEW [--no-members] [--allow-existing] [--path P]...
nextagent add-param FUNCTION "int x" [--default EXPR] [--position N] [--path P]...
nextagent add-include TARGET HEADER [--system]
nextagent edit [--request FILE]            # JSON {ops:[{path,start,end,text}],if_rev} with codepoint offsets
```

Positions in `edit` are Unicode codepoints, zero-based, end-exclusive. Everything in one request refers to the original
text; do not shift offsets for earlier operations in the same request.

## History and files changed outside

```
nextagent history [--limit N]    nextagent undo [--rev N]    nextagent redo
nextagent sync [PATH...]         # write unflushed revisions to disk (normally automatic)
nextagent merge [PATH...] [--ours|--theirs] [--dry-run]    # three-way merge of files edited outside nextagent
```

Undo is a new revision (history is append-only). `--if-rev N` makes a write fail with `stale_rev` if the workspace moved
on since you looked.

## Errors

| Message starts with | Meaning | Do |
|---|---|---|
| `text_not_found` | the old text is not in the file | `view` the place again; the text may have changed |
| `ambiguous_symbol` / `ambiguous_text` | several matches | add `--kind`/`--scope`, more context, or use `all` |
| `symbol_not_found` | no such symbol | `outline` the file; check spelling and `--kind` |
| `already_exists` | `create` on an existing path | use `replace` or pick another path |
| `stale_rev` | the workspace moved since `--if-rev` | look again, then repeat |
| `disk_conflict` | a file changed outside nextagent and could not be merged | `nextagent merge PATH` and resolve |
| `no_language_server` | rename/references needs a server for this language | use `serve --lsp` / `mcp --lsp` |
| `invalid parameter` / `missing parameter` | wrong command line | the message shows the synopsis |

A failed call never applies part of an edit; it is always safe to repeat after fixing the cause.

## Workspace files

| File | Purpose |
|---|---|
| `.aed/journal.aedj` | the checksummed history of every revision |
| `.aed/index.cache` | symbols and search index, rebuilt automatically |
| `.aed/lsp.json` | language servers (`{"servers":[{"name","command","args","extensions"}]}`) |
| `.aed/embed.json` | optional embedding model for `locate` (`url` or `command`, `model`, `doc_prefix`, `query_prefix`) |
| `.aed/embed.cache` | cached embeddings |

Add `.aed/` to `.gitignore`.
