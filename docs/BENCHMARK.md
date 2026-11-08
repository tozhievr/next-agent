# Benchmark: generic agent tools vs nextagent

`tests/bench.py` runs six code tasks on the C sources of QuickJS (14 files, 88k lines) and compares what a coding agent
has to write and read (bytes; tokens are estimated as bytes / 4).

**It is a model, not a live agent run.** nextagent is executed for real (and its result is verified: 23 renamed sites, no
old name left, 6 insertions in place). The generic side is computed with a fixed protocol from real `grep` output and
real file sizes:

- `Grep`: one call, ~70 bytes written, the real `grep -rn` output (first 250 lines) read;
- `Read`: ~50 bytes written, line-numbered text read (+7 bytes per line); "whole-file" reads cap at 2000 lines, "60-line"
  reads take a window;
- `Edit`: old + new text + 80 bytes written, ~60 bytes read; the file must have been read first.

| task | generic calls | nextagent calls | tokens generic (60-line reads) | tokens nextagent | gain |
|---|---|---|---|---|---|
| T1 find where regexp compile lives (`locate`) | 2 | 1 | 1415 | 180 | x7.9 |
| T2 read one function (`view --symbol`) | 3 | 1 | 2501 | 930 | x2.7 |
| T3 who calls a function (`callers`) | 1 | 1 | 449 | 274 | x1.6 |
| T4 rename a function, 23 sites in 4 files (`rename`) | 9 | 1 | 3066 | 35 | x88 |
| T5 six insertions in three files (`apply`) | 9 | 1 | 2182 | 115 | x19 |
| T6 gather context for a question (`context`) | 5 | 1 | 3790 | 3674 | x1.0 |
| **total** | **29** | **6** | **13403** | **5208** | **x2.6** |

With whole-file reads before each edit the generic total is 100000 tokens (x19).

## How to read it

- The gain comes from edits that touch many places (rename, batch of edits): one call, no file has to be read to
  produce them, the answer is a few bytes. Reading tasks gain 1.6-8x; `context` is a wash (same amount of code, one call
  instead of five).
- The generic side is deliberately not the cheapest possible: a shell one-liner (`grep -rl X | xargs sed -i ...`) does T4
  in about two calls and ~100 tokens, but without checking that the occurrences are the same symbol, without a revision to
  undo, and without noticing a name inside a string or comment. The comparison says what nextagent saves against the
  Grep/Read/Edit toolset, not against a careful shell session.
- Wall time is the same order of magnitude (tens to a few hundred ms per call); nextagent's first call in a fresh
  workspace pays for building `.aed/index.cache`.
- What this does not measure: how often a model fails or retries with each toolset, and the quality of `locate` answers
  (T1 uses one query; ranking quality is checked separately with a real embedding model).

Run it: `python tests/bench.py <nextagent> <quickjs-dir> <scratch-dir>`.


## Ranking quality of `locate`

`tests/locate_eval.py` asks 28 questions about QuickJS (for example "list the entries of a directory", answer
`js_os_readdir`) and checks at which rank the one function that defines the answer appears. The ground truth is strict:
a reasonable alternative (`js_evalScript` for "evaluate a script string", which calls the expected `JS_Eval`) counts as a
miss. Embedding model: `nomic-embed-text` through Ollama (5340 chunks embedded in about 110 s, once; later queries
answer in 0.2-0.5 s).

| mode | top 1 | top 3 | top 10 | MRR |
|---|---|---|---|---|
| local embeddings only (no setup) | 2/28 | 9/28 | 13/28 | 0.19 |
| + `nomic-embed-text` | 5/28 | 9/28 | 18/28 | 0.30 |

Read it as "usable for narrowing down, not for blind trust": the right function is in the first ten in about two of
three cases with the model and in under half without, and it is first in one case of five. The weight of the external
model in the fusion was tuned on these same questions (16 -> 18 in the top ten), so the table flatters the model slightly;
other weightings and the `doc_prefix`/`query_prefix` setting changed the result by one or two questions, which is noise
at this sample size. A file larger than 2 MB used to be skipped silently (QuickJS's `quickjs.c` is 2.2 MB, and most of
the answers live in it): the first run of this evaluation scored 4/28 in the top ten for that reason; the limit is now 8 MB.
