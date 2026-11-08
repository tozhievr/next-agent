#!/usr/bin/env python3
"""Modelled benchmark: the same code-change tasks done the way a coding agent does them with generic tools
(Grep / Read / Edit) versus with nextagent. nextagent is executed for real; the generic side is computed from
real grep output and real file sizes under a fixed protocol (see BASELINE below). Cost = bytes the model has
to write (commands) + bytes it has to read (tool output); tokens ~ bytes / 4.

usage: bench.py <nextagent.exe> <source-dir> <scratch-dir>
"""
import os, re, shutil, subprocess, sys, time, json

NA, SRC, SCR = sys.argv[1], sys.argv[2], os.path.abspath(sys.argv[3])
WS = os.path.join(SCR, "qjs")
shutil.rmtree(WS, ignore_errors=True)
os.makedirs(WS, exist_ok=True)
for n in sorted(os.listdir(SRC)):
    if n.endswith((".c", ".h")):
        shutil.copy(os.path.join(SRC, n), os.path.join(WS, n))
SRCFILES = sorted(n for n in os.listdir(WS))

# BASELINE protocol (Claude-Code-style generic tools):
#  Grep  : emits ~70 B, receives the real `grep -rn` output (first 250 lines)
#  Read  : emits ~50 B, receives line-numbered text (+7 B/line); a whole file is capped at 2000 lines, a window is N lines
#  Edit  : emits old_string + new_string + 80 B; receives ~60 B; needs the file Read first (once per file)
TOOL_ERR = 60


class Cost:
    def __init__(self, name):
        self.name, self.calls, self.out, self.inn, self.ms = name, 0, 0, 0, 0

    def add(self, emitted, received, ms=0):
        self.calls += 1
        self.out += emitted
        self.inn += received
        self.ms += ms

    @property
    def tokens(self):
        return (self.out + self.inn) // 4


def grep(c, pattern, word=True):
    args = ["grep", "-rn" + ("w" if word else ""), "--include=*.c", "--include=*.h", pattern, "."]
    t = time.time()
    r = subprocess.run(args, cwd=WS, capture_output=True, text=True, encoding="utf-8", errors="replace")
    ms = (time.time() - t) * 1000
    lines = r.stdout.splitlines()[:250]
    c.add(70, len("\n".join(lines)), ms)
    return [l.split(":", 2) for l in lines]


def read_file(c, name, window=None, around=None):
    text = open(os.path.join(WS, name), encoding="utf-8", errors="replace").read().splitlines()
    if window is None and not FULL[0]:
        window, around = 60, 1
    if window is None:
        part = text[:2000]
    else:
        a = max(0, (around or 1) - 1 - window // 4)
        part = text[a:a + window]
    c.add(50, sum(len(l) + 7 for l in part))
    return text


def edit(c, old, new):
    c.add(len(old) + len(new) + 80, TOOL_ERR)


def na(c, *args, stdin=None):
    t = time.time()
    r = subprocess.run([NA, "--root", WS, *args], capture_output=True, text=True, encoding="utf-8", errors="replace", input=stdin)
    ms = (time.time() - t) * 1000
    cmd = "nextagent " + " ".join(args) + (("\n" + stdin) if stdin else "")
    c.add(len(cmd), len(r.stdout) + len(r.stderr), ms)
    return r


results = []
FULL = [True]


def task(name, base_fn, na_fn):
    b, w, n = Cost(name + " [generic, whole-file reads]"), Cost(name + " [generic, 60-line reads]"), Cost(name + " [nextagent]")
    FULL[0] = True
    base_fn(b)
    FULL[0] = False
    base_fn(w)
    na_fn(n)
    results.append((name, b, w, n))


# ---- T1: find where something is implemented (read-only) ----
def t1_base(c):
    hits = grep(c, "lre_compile", word=True)
    f = next((h[0][2:] for h in hits if h[0].endswith(".c")), "libregexp.c")
    read_file(c, f, window=150, around=1)


def t1_na(c):
    na(c, "locate", "compile a regular expression into bytecode", "--limit", "3")


task("T1 find where regexp compile lives", t1_base, t1_na)


# ---- T2: understand one function (read-only) ----
def t2_base(c):
    hits = grep(c, "^int lre_compile", word=False) or grep(c, "lre_compile", word=False)
    f, ln = hits[0][0][2:], int(hits[0][1])
    read_file(c, f, window=200, around=ln)


def t2_na(c):
    na(c, "view", "libregexp.c", "--symbol", "lre_compile")


task("T2 read one function", t2_base, t2_na)


# ---- T3: callers ----
def t3_base(c):
    grep(c, "lre_realloc")


def t3_na(c):
    na(c, "callers", "lre_realloc")


task("T3 who calls lre_realloc", t3_base, t3_na)


# ---- T4: rename a function across the workspace ----
def t4_base(c):
    hits = grep(c, "lre_realloc")
    files = sorted({h[0][2:] for h in hits})
    for f in files:
        read_file(c, f)  # an Edit needs the file read first
        edit(c, "lre_realloc", "lre_xalloc")  # replace_all per file


def t4_na(c):
    na(c, "rename", "lre_realloc", "lre_xalloc")


task("T4 rename lre_realloc -> lre_xalloc (23 sites)", t4_base, t4_na)


# ---- T5: six small edits in three files ----
EDITS = [("libregexp.c", "lre_compile", "/* audited */"), ("libregexp.c", "lre_exec", "/* audited */"),
         ("libunicode.c", "lre_case_conv", "/* audited */"), ("libunicode.c", "unicode_normalize", "/* audited */"),
         ("dtoa.c", "js_atod", "/* audited */"), ("dtoa.c", "js_dtoa", "/* audited */")]


def t5_base(c):
    seen = set()
    for f, sym, text in EDITS:
        if f not in seen:
            seen.add(f)
            read_file(c, f)
        edit(c, "static int " + sym + "(unique context line)", text + "\nstatic int " + sym + "(unique context line)")


def t5_na(c):
    script = "".join("@@@ insert %s before-symbol %s\n%s\n" % (f, sym, text) for f, sym, text in EDITS)
    na(c, "apply", "-", stdin=script)


task("T5 six one-line insertions in three files", t5_base, t5_na)


# ---- T6: a question answered with a budgeted code pack ----
def t6_base(c):
    grep(c, "lre_realloc")
    hits = grep(c, "lre_compile")
    for f in [h[0][2:] for h in hits[:3]]:
        read_file(c, f, window=150, around=1)


def t6_na(c):
    na(c, "context", "how does the regexp compiler allocate memory", "--budget", "3000")


task("T6 gather context for a question", t6_base, t6_na)

# ---- verification of what nextagent actually did ----
def count(pat):
    r = subprocess.run(["grep", "-rwo", "--include=*.c", "--include=*.h", pat, "."], cwd=WS, capture_output=True, text=True)
    return len(r.stdout.splitlines())
def count_txt(pat):
    r = subprocess.run(["grep", "-rF", "--include=*.c", "--include=*.h", pat, "."], cwd=WS, capture_output=True, text=True)
    return len(r.stdout.splitlines())
print("check: lre_realloc left=%d (want 0), lre_xalloc=%d (want 23), '/* audited */'=%d (want 6)" % (count("lre_realloc"), count("lre_xalloc"), count_txt("/* audited */")))

# ---- report ----
print("%-62s %5s %8s %8s %7s" % ("task", "calls", "bytes>", "bytes<", "tokens"))
tot = [0, 0, 0]
for name, b, w, n in results:
    print(name)
    for c in (b, w, n):
        print("  %-60s %5d %8d %8d %7d  %s" % (c.name[len(name) + 1:], c.calls, c.out, c.inn, c.tokens, "%d ms" % c.ms if c.ms else ""))
    print("  -> tokens vs whole-file x%.1f, vs 60-line reads x%.1f" % (b.tokens / max(1, n.tokens), w.tokens / max(1, n.tokens)))
    tot[0] += b.tokens; tot[1] += w.tokens; tot[2] += n.tokens
print("TOTAL tokens: generic(whole-file) %d, generic(60-line) %d, nextagent %d" % tuple(tot))
print("ratio: x%.1f / x%.1f" % (tot[0] / max(1, tot[2]), tot[1] / max(1, tot[2])))
print("calls: generic %d, nextagent %d" % (sum(b.calls for _, b, _, _ in results), sum(n.calls for _, _, _, n in results)))
