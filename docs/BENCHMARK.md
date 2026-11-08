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
