#!/usr/bin/env python3
"""Cross-platform parity check: runs one scripted session against a nextagent binary and prints a
transcript. The transcript must be byte-identical on Windows and Linux (it is compared with
tests/parity.golden in CI).

usage: python tests/parity.py <nextagent-binary> [--update]
"""
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
GOLDEN = os.path.join(HERE, "parity.golden")

FILES = {
    "a.h": "#ifndef A_H\n#define A_H\nint calc(int x);\n#endif\n",
    "a.c": '#include "a.h"\n/* calc(1) in a comment éè */\nint calc(int x) { return x * 2; }\n'
           "int run(void) { return calc(calc(3) + 1); }\nstruct P { int x; int y; };\n",
    "crlf.c": "int q;\r\nint r;\r\n",
}


def strip_volatile(text):
    return re.sub(r'"ts":\d+', '"ts":0', text)


def main():
    exe = os.path.abspath(sys.argv[1])
    ws = tempfile.mkdtemp(prefix="na-parity-")
    for name, text in FILES.items():
        with open(os.path.join(ws, name), "w", encoding="utf-8", newline="") as f:
            f.write(text)
    lines = []

    def run(*args, stdin=None):
        r = subprocess.run([exe, "--root", ws] + list(args), input=stdin, capture_output=True)
        r.stdout = r.stdout.decode("utf-8")
        r.stderr = r.stderr.decode("utf-8")
        lines.append("$ nextagent " + " ".join(args))
        lines.append("exit=%d" % r.returncode)
        lines.append(strip_volatile(r.stdout).replace(ws.replace("\\", "/"), "<ws>").rstrip())
        err = r.stderr.strip().replace(ws.replace("\\", "/"), "<ws>")
        if err:
            lines.append("stderr: " + err)
        return r

    run("outline", "a.c")
    run("outline", "a.c", "--nested")
    run("select", "a.c", "calc")
    run("references", "calc")
    run("rename", "calc", "area")
    run("view", "a.c")
    run("add-param", "area", "int mode", "--default", "0")
    run("add-include", "a.c", "stdlib.h", "--system")
    run("view", "a.c")
    run("history")
    run("undo")
    run("view", "a.c")
    run("redo")
    run("find", "ar")
    run("sync")
    for name in sorted(FILES):
        with open(os.path.join(ws, name), "rb") as f:
            data = f.read()
        lines.append("file %s sha256=%s" % (name, hashlib.sha256(data).hexdigest()))
    # the journal must replay to the same revision count after a restart
    run("history")
    shutil.rmtree(ws, ignore_errors=True)
    out = "\n".join(lines) + "\n"
    if "--update" in sys.argv:
        with open(GOLDEN, "w", encoding="utf-8", newline="\n") as f:
            f.write(out)
        print("updated", GOLDEN)
        return 0
    with open(GOLDEN, encoding="utf-8", newline="") as f:
        want = f.read()
    if out != want:
        sys.stdout.write(out)
        print("PARITY MISMATCH against tests/parity.golden", file=sys.stderr)
        return 1
    print("parity ok (%d lines)" % len(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())
