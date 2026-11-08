#!/usr/bin/env python3
"""Live check of language servers through `nextagent serve --lsp --no-gate`.
usage: lspcheck.py <nextagent> <scratch-dir>
Needs `npm install pyright typescript typescript-language-server` in <scratch-dir>/lsp and `npm install typescript@5` in <scratch-dir>/ts_ws.
Creates a Python and a TypeScript workspace, points .aed/lsp.json at the locally installed servers and
exercises lsp_servers, outline, find_symbols, references and rename. Prints one line per step."""
import json, os, shutil, subprocess, sys, time

NA, SCR = sys.argv[1], os.path.abspath(sys.argv[2])
LSP = os.path.join(SCR, "lsp", "node_modules")

PY = {
    "mod.py": "class Account:\n    def __init__(self, owner):\n        self.owner = owner\n        self.items = []\n\n    def add(self, price):\n        self.items.append(price)\n\n\ndef compute_total(account):\n    return sum(account.items)\n",
    "app.py": "from mod import Account, compute_total\n\n\ndef main():\n    acc = Account('bob')\n    acc.add(3)\n    print(compute_total(acc))\n\n\nif __name__ == '__main__':\n    main()\n",
}
TS = {
    "tsconfig.json": '{"compilerOptions":{"target":"es2020","module":"commonjs","strict":true}}\n',
    "lib.ts": "export interface Item { price: number }\n\nexport class Account {\n  items: Item[] = [];\n  add(price: number): void { this.items.push({ price }); }\n}\n\nexport function computeTotal(a: Account): number {\n  return a.items.reduce((s, i) => s + i.price, 0);\n}\n",
    "app.ts": "import { Account, computeTotal } from './lib';\n\nconst acc = new Account();\nacc.add(3);\nconsole.log(computeTotal(acc));\n",
}
CASES = [
    ("python/pyright", "py_ws", PY, "pyright",
     os.path.join(LSP, "pyright", "langserver.index.js") + " --stdio", [".py", ".pyi"],
     "mod.py", "compute_total", "compute_sum", "app.py"),
    ("typescript", "ts_ws", TS, "typescript",
     os.path.join(LSP, "typescript-language-server", "lib", "cli.mjs") + " --stdio", [".ts", ".tsx"],
     "lib.ts", "computeTotal", "computeSum", "app.ts"),
]


def run_case(label, wsdir, files, sname, sargs, exts, main_file, sym, newsym, user_file):
    ws = os.path.join(SCR, wsdir)
    shutil.rmtree(os.path.join(ws, ".aed"), ignore_errors=True)
    os.makedirs(os.path.join(ws, ".aed"), exist_ok=True)
    for n, t in files.items():
        with open(os.path.join(ws, n), "w", newline="\n") as f:
            f.write(t)
    with open(os.path.join(ws, ".aed", "lsp.json"), "w") as f:
        json.dump({"servers": [{"name": sname, "command": "node", "args": sargs, "extensions": exts}]}, f)
    p = subprocess.Popen([NA, "--root", ws, "serve", "--lsp", "--no-gate"], stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8")
    nid = [0]

    def call(method, **params):
        nid[0] += 1
        t0 = time.time()
        p.stdin.write(json.dumps({"jsonrpc": "2.0", "id": nid[0], "method": method, "params": params}) + "\n")
        p.stdin.flush()
        line = p.stdout.readline()
        dt = int((time.time() - t0) * 1000)
        try:
            r = json.loads(line)
        except Exception:
            return dt, None, "no/invalid reply: %r" % line[:200]
        if "error" in r:
            return dt, None, json.dumps(r["error"])[:300]
        return dt, r["result"], None

    def step(label_, ok_fn, method, **params):
        dt, res, err = call(method, **params)
        ok = False
        info = err or ""
        if res is not None:
            try:
                ok, info = ok_fn(res)
            except Exception as e:
                info = "check failed: %r on %s" % (e, json.dumps(res)[:200])
        print("  %-5s %-22s %6d ms  %s" % ("OK" if ok else "FAIL", label_, dt, info))
        return res

    print("== %s" % label)
    step("lsp_servers", lambda r: (r.get("enabled") is True, json.dumps(r.get("servers"))[:140]), "lsp_servers")
    step("outline", lambda r: (any(s["name"] == sym for s in r["symbols"]),
                               "%d symbols: %s" % (len(r["symbols"]), ",".join(s["name"] for s in r["symbols"])[:80])),
         "outline", target=main_file)
    step("find_symbols", lambda r: (any(s["name"] == sym for s in r.get("symbols", r.get("matches", []))), "count=%s" % r.get("count")),
         "find_symbols", query=sym)
    step("references", lambda r: (len(r.get("references", [])) >= 2,
                                  "%d refs in %s src=%s" % (len(r.get("references", [])),
                                                            sorted({x["path"] for x in r.get("references", [])}), r.get("source"))),
         "references", name=sym)
    step("rename", lambda r: (len(r.get("paths", r.get("files", []))) >= 2, json.dumps(r)[:160]),
         "rename", name=sym, new_name=newsym)
    step("view user after", lambda r: (newsym in r.get("text", json.dumps(r)) and sym not in r.get("text", json.dumps(r)),
                                       "new name present, old gone"), "view", target=user_file)
    p.stdin.close()
    try:
        p.wait(timeout=10)
    except Exception:
        p.kill()
    err = p.stderr.read()
    if err.strip():
        print("  stderr:", err.strip()[:4000])


for c in CASES:
    run_case(*c)


# ---- one-shot CLI (no `serve`): symbols and refactorings go through the language servers too ----
def cli_case(label, wsdir, files, sname, sargs, exts, main_file, sym, newsym, user_file):
    ws = os.path.join(SCR, wsdir + "_cli")
    shutil.rmtree(os.path.join(ws, ".aed"), ignore_errors=True)
    os.makedirs(os.path.join(ws, ".aed"), exist_ok=True)
    for n, t in files.items():
        with open(os.path.join(ws, n), "w", newline="\n") as f:
            f.write(t)
    with open(os.path.join(ws, ".aed", "lsp.json"), "w") as f:
        json.dump({"servers": [{"name": sname, "command": "node", "args": sargs, "extensions": exts}]}, f)
    nm = os.path.join(SCR, wsdir)
    if os.path.isdir(os.path.join(nm, "node_modules")) and not os.path.exists(os.path.join(ws, "node_modules")):
        try:
            os.symlink(os.path.join(nm, "node_modules"), os.path.join(ws, "node_modules"), target_is_directory=True)
        except OSError:
            shutil.copytree(os.path.join(nm, "node_modules"), os.path.join(ws, "node_modules"))
    print("== %s, one-shot CLI" % label)

    def cli(name, ok_fn, *args):
        t0 = time.time()
        r = subprocess.run([NA, "--root", ws, "--brief", *args], capture_output=True, text=True, encoding="utf-8", errors="replace")
        dt = int((time.time() - t0) * 1000)
        ok, info = ok_fn(r.stdout, r.stderr, r.returncode)
        print("  %-5s %-22s %6d ms  %s" % ("OK" if ok else "FAIL", name, dt, info))

    cli("outline", lambda o, e, c: (sym in o, o.strip().replace("\n", " | ")[:80] or e[:100]), "outline", main_file)
    cli("view --symbol", lambda o, e, c: (sym in o, o.strip()[:60].replace("\n", " ") or e[:100]), "view", main_file, "--symbol", sym, "--raw")
    cli("references", lambda o, e, c: (user_file in o and main_file in o, o.strip().replace("\n", " | ")[:100] or e[:100]), "references", sym)
    cli("rename", lambda o, e, c: (c == 0, (o or e).strip()[:100]), "rename", sym, newsym)
    with open(os.path.join(ws, user_file), encoding="utf-8") as f:
        u = f.read()
    with open(os.path.join(ws, main_file), encoding="utf-8") as f:
        m = f.read()
    ok = newsym in u and sym not in u and newsym in m and sym not in m
    print("  %-5s %-22s %6s     %s" % ("OK" if ok else "FAIL", "files on disk", "", "both files renamed" if ok else "not renamed"))


for c in CASES:
    cli_case(*c)
