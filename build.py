#!/usr/bin/env python3
"""build.py - build NextAgent with clang + libc23 (freestanding, no system libc / SDK) for Windows or Linux.

usage:
    python build.py [build|test|clean] [options]

options:
    --target windows|linux     output platform (default: host OS)
    --cc PATH                  clang executable (default: clang from PATH)
    --libc DIR                 libc23 checkout (default: $LIBC23 or ./vendor/libc23)
    --config release|debug     (default: release)
    --jobs N                   parallel compile jobs (default: CPU count)
    --out DIR                  output directory (default: build/<target>)

Outputs:  <out>/nextagent[.exe]  and  <out>/tests[.exe]  (tests are built and run by `test`).
The result is byte-for-byte reproducible per target and behaves identically on both platforms: the
same source, the same libc23, only the thin platform layer (src/platform_win.c / platform_posix.c) differs.
"""
import argparse
import concurrent.futures
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))

LIB = ["util", "json", "platform", "core", "store", "c23", "lsp_proto", "lsp_build", "lsp_client", "lsp_gate",
       "lsp_hub", "refactor", "rpc"]


def norm(p):
    return p.replace("\\", "/")


def find_tool(name, cc):
    d = os.path.dirname(shutil.which(cc) or cc)
    for cand in (os.path.join(d, name), os.path.join(d, name + ".exe")):
        if os.path.exists(cand):
            return cand
    p = shutil.which(name)
    if not p:
        sys.exit("build.py: cannot find '%s' (it ships with LLVM)" % name)
    return p


def run(cmd, **kw):
    r = subprocess.run(cmd, **kw)
    if r.returncode != 0:
        sys.exit("build.py: command failed (%d): %s" % (r.returncode, " ".join(cmd[:4])))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", nargs="?", default="build", choices=["build", "test", "clean"])
    ap.add_argument("--target", default="windows" if os.name == "nt" else "linux", choices=["windows", "linux"])
    ap.add_argument("--cc", default=os.environ.get("CLANG", "clang"))
    ap.add_argument("--libc", default=os.environ.get("LIBC23", os.path.join(ROOT, "vendor", "libc23")))
    ap.add_argument("--config", default="release", choices=["release", "debug"])
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    ap.add_argument("--out", default="")
    a = ap.parse_args()

    out = os.path.abspath(a.out or os.path.join(ROOT, "build", a.target))
    if a.command == "clean":
        shutil.rmtree(out, ignore_errors=True)
        return 0
    os.makedirs(os.path.join(out, "obj"), exist_ok=True)
    cc = shutil.which(a.cc) or a.cc
    libc = os.path.abspath(a.libc)
    if not os.path.exists(os.path.join(libc, "build.py")):
        sys.exit("build.py: libc23 not found at %s (use --libc or $LIBC23)" % libc)

    # 1. libc23 (its own build script; only the library, no tests)
    run([sys.executable, os.path.join(libc, "build.py"), "build", "--platform", a.target, "--no-tests",
         "--executor", "ninja", "--config", a.config, "--cc", cc])
    libdir = os.path.join(libc, "build", "%s-x86_64-%s" % (a.target, a.config))

    triple = "x86_64-pc-windows-msvc" if a.target == "windows" else "x86_64-unknown-linux-gnu"
    flags = ["--target=" + triple, "-std=c23", "-ffreestanding", "-fno-builtin", "-nostdinc",
             "-isystem", norm(os.path.join(libc, "include")), "-I" + norm(os.path.join(ROOT, "src")),
             "-DNA_LIBC23", "-fno-stack-protector", "-fno-stack-check", "-fno-unwind-tables",
             "-fno-asynchronous-unwind-tables", "-ffp-contract=off", "-fno-strict-aliasing", "-fwrapv",
             "-fno-short-wchar", "-ffunction-sections", "-fdata-sections", "-Wall", "-Wno-unknown-pragmas",
             "-Wno-builtin-macro-redefined", "-U__STDC_NO_THREADS__"]
    flags += ["-O2", "-DNDEBUG"] if a.config == "release" else ["-O0", "-g"]
    if a.target == "linux":
        flags += ["-fno-pic", "-fno-pie"]

    # 2. compile
    srcs = ["src/%s.c" % n for n in LIB] + ["src/main.c", "tests/tests.c"]
    jobs = []
    for s in srcs:
        o = os.path.join(out, "obj", s.replace("/", "_") + ".o")
        jobs.append((s, o))

    def compile_one(job):
        s, o = job
        src = os.path.join(ROOT, s)
        if os.path.exists(o) and os.path.getmtime(o) > max(os.path.getmtime(src), newest_header):
            return 0
        r = subprocess.run([cc] + flags + ["-c", norm(src), "-o", o])
        return r.returncode

    newest_header = max(os.path.getmtime(os.path.join(ROOT, "src", f)) for f in os.listdir(os.path.join(ROOT, "src"))
                        if f.endswith(".h"))
    with concurrent.futures.ThreadPoolExecutor(a.jobs) as ex:
        if any(ex.map(compile_one, jobs)):
            sys.exit("build.py: compilation failed")

    # 3. link
    exe = ".exe" if a.target == "windows" else ""
    lib_objs = [o for s, o in jobs if "main.c" not in s and "tests.c" not in s]
    main_o = [o for s, o in jobs if s.endswith("main.c")][0]
    test_o = [o for s, o in jobs if s.endswith("tests.c")][0]
    products = [("nextagent" + exe, main_o), ("tests" + exe, test_o)]
    resdir = subprocess.check_output([cc, "-print-resource-dir"], text=True).strip()
    implib = os.path.join(out, "kernel32_na.lib")
    if a.target == "windows":  # the Win32 calls of src/platform_win.c that libc23's own kernel32.def lacks
        run([find_tool("llvm-dlltool", cc), "-m", "i386:x86-64", "-d", os.path.join(ROOT, "src", "win_imports.def"),
             "-l", implib])
    for name, entry in products:
        target = os.path.join(out, name)
        if a.target == "windows":
            lld = find_tool("lld-link", cc)
            libs = [os.path.join(libdir, x) for x in ("libc23.lib", "kernel32.lib", "synch.lib", "bcryptprimitives.lib")]
            libs.append(implib)
            cmd = [lld, "/nologo", "/subsystem:console", "/entry:__libc_mainCRTStartup", "/nodefaultlib",
                   "/opt:ref", "/brepro", "/out:" + target] + lib_objs + [entry] + libs
        else:
            lld = find_tool("ld.lld", cc)
            rt = os.path.join(resdir, "lib", "linux", "libclang_rt.builtins-x86_64.a")
            libs = [os.path.join(libdir, "libc23.a")] + ([rt] if os.path.exists(rt) else [])
            cmd = [lld, "-static", "-nostdlib", "-e", "_start", "--gc-sections", "--build-id=none",
                   "-o", target] + lib_objs + [entry] + libs
        run(cmd)
        print("built", norm(os.path.relpath(target, ROOT)))

    # 4. test
    if a.command == "test":
        t = os.path.join(out, "tests" + exe)
        if a.target == "linux" and os.name == "nt":
            drive, rest = os.path.splitdrive(t)
            t = ["wsl", "/mnt/%s%s" % (drive[0].lower(), norm(rest))]
        else:
            t = [t]
        env = dict(os.environ, MSYS_NO_PATHCONV="1")
        r = subprocess.run(t, env=env)
        return r.returncode
    return 0


if __name__ == "__main__":
    sys.exit(main())
