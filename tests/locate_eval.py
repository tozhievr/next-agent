#!/usr/bin/env python3
"""Ranking quality of `nextagent locate` on a real code base (QuickJS) with known answers.
Modes: local (built-in random-indexing embeddings only) and external (model from .aed/embed.json, e.g. Ollama).

usage: locate_eval.py <nextagent> <quickjs-dir> <scratch-dir> [--url URL --model NAME] [--prefix]
Prints per-question ranks and top1/top3/top10/MRR for each mode."""
import json, os, shutil, subprocess, sys, time

NA, SRC, SCR = sys.argv[1], sys.argv[2], os.path.abspath(sys.argv[3])
opts = sys.argv[4:]
def opt(name, default):
    return opts[opts.index(name) + 1] if name in opts else default
URL = opt("--url", "http://127.0.0.1:11434/v1/embeddings")
MODEL = opt("--model", "nomic-embed-text")

# (question, symbol that defines the answer)
QS = [
    ("compile a regular expression pattern into bytecode", "lre_compile"),
    ("execute compiled regex bytecode against a subject string and fill the captures", "lre_exec"),
    ("convert a unicode code point to upper or lower case", "lre_case_conv"),
    ("normalize a unicode string to NFC or NFD form", "unicode_normalize"),
    ("parse a decimal string into a double with a given radix", "js_atod"),
    ("format a floating point number as a decimal string", "js_dtoa"),
    ("read the whole contents of a file into memory", "js_load_file"),
    ("event loop that runs timers and pending promise jobs", "js_std_loop"),
    ("parse JSON text into a JavaScript value", "JS_ParseJSON"),
    ("create a new empty JavaScript object", "JS_NewObject"),
    ("run precompiled bytecode loaded from a buffer", "js_std_eval_binary"),
    ("Array.prototype.push implementation", "js_array_push"),
    ("String indexOf substring search", "js_string_indexOf"),
    ("pseudo random number generator for Math.random", "js_math_random"),
    ("serialize a value to JSON text", "js_json_stringify"),
    ("get a property of an object by its C string name", "JS_GetPropertyStr"),
    ("schedule a callback to run after a delay", "js_os_setTimeout"),
    ("wait for a promise to settle synchronously", "js_std_await"),
    ("list the entries of a directory", "js_os_readdir"),
    ("create a new empty array", "JS_NewArray"),
    ("sort the elements of an array with a comparison function", "js_array_sort"),
    ("strip whitespace from both ends of a string", "js_string_trim"),
    ("run the exec method of a regexp object on a string", "js_regexp_exec"),
    ("convert a value to boolean truthiness", "JS_ToBool"),
    ("Proxy get trap handler", "js_proxy_get"),
    ("evaluate a script string and return the result", "JS_Eval"),
    ("print an uncaught exception with its stack trace", "js_std_dump_error"),
    ("spawn a child process and wait for it to finish", "js_os_exec"),
]


def make_ws(name, external):
    ws = os.path.join(SCR, name)
    if "--reuse" in opts and os.path.isdir(ws):
        return ws
    shutil.rmtree(ws, ignore_errors=True)
    os.makedirs(ws, exist_ok=True)
    for n in sorted(os.listdir(SRC)):
        if n.endswith((".c", ".h")):
            shutil.copy(os.path.join(SRC, n), os.path.join(ws, n))
    if external:
        os.makedirs(os.path.join(ws, ".aed"), exist_ok=True)
        with open(os.path.join(ws, ".aed", "embed.json"), "w") as f:
            json.dump({"url": URL, "model": MODEL}, f)
    return ws


def run_mode(label, external):
    ws = make_ws("eval_" + label, external)
    ranks = []
    t_first = None
    for qi, (q, want) in enumerate(QS):
        args = [NA, "--root", ws, "locate", q, "--limit", "10"]
        if external:
            args += ["--embed", "100000"]
        t = time.time()
        r = subprocess.run(args, capture_output=True, text=True, encoding="utf-8", errors="replace")
        dt = time.time() - t
        if qi == 0:
            t_first = dt
        try:
            res = json.loads(r.stdout)
            syms = [m.get("symbol") for m in res.get("matches", [])]
            emb = res.get("embedding")
        except Exception:
            syms, emb = [], None
            print("  !! bad output for %r: %s %s" % (q, r.stdout[:150], r.stderr[:150]))
        rank = syms.index(want) + 1 if want in syms else 0
        ranks.append(rank)
        print("  %-3s %-72s %-22s rank=%s  %.1fs%s" % ("OK" if rank == 1 else "..", q[:72], want, rank or "-", dt,
                                                    ("  emb=" + json.dumps(emb)) if emb and qi < 2 else ""))
    n = len(ranks)
    top = lambda k: sum(1 for x in ranks if 0 < x <= k)
    mrr = sum(1.0 / x for x in ranks if x) / n
    print("== %s: top1 %d/%d  top3 %d/%d  top10 %d/%d  MRR %.3f  (first query %.1fs)" % (label, top(1), n, top(3), n, top(10), n, mrr, t_first))
    return ranks


if "--only-external" not in opts:
    print("local (no external model)")
    run_mode("local", False)
print("external (%s via %s)" % (MODEL, URL))
run_mode("external", True)
