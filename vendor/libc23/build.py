#!/usr/bin/env python3
"""build.py - build libc23 with the system clang (no SDK / no Visual Studio).

usage:
    python build.py [build] [options]      build library (+ tests)
    python build.py test [options] [name]  build and run tests (name = glob on tests/<name>.c, e.g. 'string_*')
    python build.py clean [options]

options:
    --platform windows|linux     platform backend (default: host OS)
    --arch x86_64|aarch64        target architecture (default: x86_64)
    --config release|debug       (default: release)
    --cc PATH                    clang executable (default: clang from PATH)
    --jobs N                     parallel jobs for ninja
    --no-tests                   do not build tests when building
    --generate-only              write build.ninja without invoking ninja
    --executor direct|ninja       Windows defaults to direct; other hosts use ninja
    --out SUFFIX                 separate build dir build/<tag>-SUFFIX (parallel isolated builds)
    --only DIR[,DIR...]          only put these src/ subdirs (+ startup, errno, stdlib/exit.c) into the lib

Outputs are placed in  build/<platform>-<arch>-<config>/ :
    libc23.lib | libc23.a   the library (libc + libm + startup + platform layer)
    *.lib (windows)         generated import libraries (kernel32 etc.)
    tests/<name>.exe        test programs
"""
import os, sys, subprocess, shutil, glob, argparse, re

ROOT = os.path.dirname(os.path.abspath(__file__))


def run(cmd, **kw):
    return subprocess.run(cmd, **kw)


def find_tool(name, cc):
    d = os.path.dirname(shutil.which(cc) or cc)
    for cand in (os.path.join(d, name), os.path.join(d, name + ".exe")):
        if os.path.exists(cand):
            return cand
    p = shutil.which(name)
    if p:
        return p
    sys.exit("build.py: cannot find tool '%s'" % name)


# Extra compile flags for particular source directories (relative to src/).
DIR_FLAGS = {
    "math": ["-frounding-math", "-fno-math-errno", "-fno-fast-math"],
    "complex": ["-frounding-math", "-fno-math-errno", "-fno-fast-math"],
    "fenv": ["-frounding-math"],
    "stdlib": ["-frounding-math"],   # strtod & friends honour the rounding mode
    "stdio": ["-frounding-math"],    # printf/scanf floating point
    "wchar": ["-frounding-math"],
}


def norm(p):
    return p.replace("\\", "/")


def nesc(p):
    """escape a path for use in a ninja build edge"""
    return norm(p).replace("$", "$$").replace(":", "$:").replace(" ", "$ ")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", nargs="?", default="build", choices=["build", "test", "clean"])
    ap.add_argument("name", nargs="?", help="single test name for 'test'")
    ap.add_argument("--platform", default="windows" if os.name == "nt" else "linux")
    ap.add_argument("--arch", default="x86_64")
    ap.add_argument("--config", default="release", choices=["release", "debug"])
    ap.add_argument("--cc", default="clang")
    ap.add_argument("--jobs", type=int, default=0)
    ap.add_argument("--no-tests", action="store_true")
    ap.add_argument("--generate-only", action="store_true", help="write build.ninja without invoking ninja")
    ap.add_argument("--executor", choices=["direct", "ninja"],
                    default="direct" if os.name == "nt" else "ninja")
    ap.add_argument("--out", default="", help="suffix for the build directory (isolated parallel builds)")
    ap.add_argument("--only", default="", help="comma separated src/ subdirectories to include in the library "
                    "(core runtime is always included); e.g. --only string,stdbit")
    a = ap.parse_args()

    tag = "%s-%s-%s" % (a.platform, a.arch, a.config) + ("-" + a.out if a.out else "")
    bdir = os.path.join(ROOT, "build", tag)
    if a.command == "clean":
        shutil.rmtree(bdir, ignore_errors=True)
        return 0
    os.makedirs(bdir, exist_ok=True)

    cc = shutil.which(a.cc) or a.cc
    ar = find_tool("llvm-ar", cc)
    lib = find_tool("llvm-lib", cc) if a.platform == "windows" else None
    dlltool = find_tool("llvm-dlltool", cc) if a.platform == "windows" else None
    lld = find_tool("lld-link" if a.platform == "windows" else "ld.lld", cc)
    resdir = subprocess.check_output([cc, "-print-resource-dir"], text=True).strip()

    if a.platform == "windows":
        triple = {"x86_64": "x86_64-pc-windows-msvc", "aarch64": "aarch64-pc-windows-msvc"}[a.arch]
        obj, libext, exeext = ".o", ".lib", ".exe"
        dllm = {"x86_64": "i386:x86-64", "aarch64": "arm64"}[a.arch]
        rt = os.path.join(resdir, "lib", "windows", "clang_rt.builtins-%s.lib" % a.arch)
    elif a.platform == "linux":
        triple = {"x86_64": "x86_64-unknown-linux-gnu", "aarch64": "aarch64-unknown-linux-gnu"}[a.arch]
        obj, libext, exeext = ".o", ".a", ""
        rt = os.path.join(resdir, "lib", "linux", "libclang_rt.builtins-%s.a" % a.arch)
        if not os.path.exists(rt):
            rt = None
    else:
        sys.exit("unknown platform " + a.platform)

    # ---- source discovery ------------------------------------------------
    srcs = []
    for pat in ("src/**/*.c", "platform/%s/*.c" % a.platform, "arch/%s/*.c" % a.arch,
                "arch/%s/*.S" % a.arch, "platform/%s/*.S" % a.platform):
        srcs += glob.glob(os.path.join(ROOT, pat), recursive=True)
    srcs = sorted(norm(os.path.relpath(s, ROOT)) for s in srcs)
    if a.only:
        keep = set(x.strip() for x in a.only.split(",") if x.strip())
        always = {"startup", "errno", "runtime"}
        def wanted(s):
            parts = s.split("/")
            if parts[0] != "src" or len(parts) < 3:
                return True
            if parts[1] == "internal":
                return False
            return parts[1] in keep or parts[1] in always or s == "src/stdlib/exit.c"
        srcs = [s for s in srcs if wanted(s)]
    tests = sorted(norm(os.path.relpath(s, ROOT)) for s in glob.glob(os.path.join(ROOT, "tests", "*.c"))
                   if os.path.basename(s) != "next_test.c")
    if a.command == "test" and a.name:
        import fnmatch
        tests = [t for t in tests if fnmatch.fnmatch(os.path.splitext(os.path.basename(t))[0], a.name)]
        if not tests:
            sys.exit("no such test: " + a.name)

    common = [
        "--target=" + triple, "-std=c23", "-ffreestanding", "-fno-builtin", "-nostdinc",
        # Clang's Windows host-CRT default does not describe this runtime.
        "-U__STDC_NO_THREADS__", "-Wno-builtin-macro-redefined",
        "-fno-stack-protector", "-fno-stack-check", "-fno-unwind-tables", "-fno-asynchronous-unwind-tables",
        "-ffp-contract=off", "-fno-strict-aliasing", "-fwrapv",
        "-ffunction-sections", "-fdata-sections", "-fno-short-wchar",
        "-Wall", "-Wextra", "-Wno-unused-parameter", "-Wno-unknown-pragmas",
        "-isystem", norm(os.path.join(ROOT, "include")),
    ]
    if a.config == "release":
        common += ["-O2", "-DNDEBUG"]
    else:
        common += ["-O0", "-g"]
    lib_flags = common + ["-I" + norm(os.path.join(ROOT, "src/internal")), "-I" + norm(ROOT),
                          "-D_LIBC23_BUILD", "-fvisibility=hidden"]
    if "src/threads/tss.c" in srcs:
        lib_flags.append("-D_LIBC23_HAS_THREADS")
    if a.arch == "x86_64":
        lib_flags += ["-mno-red-zone"] if False else []

    def q(s):
        return '"%s"' % s if (" " in s) else s

    n = []
    w = n.append
    w("ninja_required_version = 1.8")
    w("cc = " + q(norm(cc)))
    w("ar = " + q(norm(ar)))
    if lib: w("lib = " + q(norm(lib)))
    if dlltool: w("dlltool = " + q(norm(dlltool)))
    w("lld = " + q(norm(lld)))
    w("root = " + norm(ROOT))
    w("cflags = " + " ".join(q(x) for x in lib_flags))
    w("tflags = " + " ".join(q(x) for x in common) + " -I" + norm(ROOT) + "/tests")
    w("")
    w("rule cc")
    w("  command = $cc $cflags $extra -MD -MF $out.d -c $in -o $out")
    w("  depfile = $out.d")
    w("  deps = gcc")
    w("  description = CC $in")
    w("rule tcc")
    w("  command = $cc $tflags $extra -MD -MF $out.d -c $in -o $out")
    w("  depfile = $out.d")
    w("  deps = gcc")
    w("  description = CC(test) $in")
    if a.platform == "windows":
        w("rule ar")
        w("  command = $lib /nologo /out:$out @$out.rsp")
        w("  rspfile = $out.rsp")
        w("  rspfile_content = $in")
        w("  description = LIB $out")
        w("rule implib")
        w("  command = $dlltool -m %s -d $in -l $out" % dllm)
        w("  description = IMPLIB $out")
        w("rule link")
        w("  command = $lld /nologo /subsystem:console /entry:__libc_mainCRTStartup /nodefaultlib /opt:ref %s $in $libs /out:$out" %
          ("/debug" if a.config == "debug" else ""))
        w("  description = LINK $out")
    else:
        w("rule ar")
        w("  command = $ar rcs $out @$out.rsp")
        w("  rspfile = $out.rsp")
        w("  rspfile_content = $in")
        w("  description = AR $out")
        w("rule link")
        w("  command = $lld -static -nostdlib -e _start --gc-sections $in $libs -o $out")
        w("  description = LINK $out")
    w("")

    objs = []
    for s in srcs:
        o = "obj/" + s.replace("/", "_") + obj
        parts = s.split("/")
        extra = []
        if parts[0] == "src" and len(parts) > 2:
            extra = DIR_FLAGS.get(parts[1], [])
        w("build %s: cc %s/%s" % (o, "$root", s))
        if extra:
            w("  extra = " + " ".join(extra))
        objs.append(o)
    libname = "libc23" + libext
    w("build %s: ar %s" % (libname, " ".join(objs)))

    syslibs = []
    if a.platform == "windows":
        for d in sorted(glob.glob(os.path.join(ROOT, "platform", "windows", "*.def"))):
            base = os.path.splitext(os.path.basename(d))[0]
            w("build %s.lib: implib %s/platform/windows/%s.def" % (base, "$root", base))
            syslibs.append(base + ".lib")
        libs = [libname] + syslibs + ([norm(rt)] if os.path.exists(rt) else [])
    else:
        libs = [libname] + ([norm(rt)] if rt else [])
    w("build libs: phony %s" % " ".join([libname] + syslibs))

    texes = []
    if not a.no_tests or a.command == "test":
        # next_test.c is a shared framework, not an executable.  Compile it
        # once when an executable directly includes its header.
        uses_next_test = set()
        for t in tests:
            with open(os.path.join(ROOT, t), encoding="utf-8-sig") as source:
                if re.search(r'^\s*#\s*include\s*[<"]next_test\.h[>"]', source.read(), re.MULTILINE):
                    uses_next_test.add(t)
        helper = "tests/next_test" + obj
        if uses_next_test:
            w("build %s: tcc $root/tests/next_test.c" % helper)
        for t in tests:
            name = os.path.splitext(os.path.basename(t))[0]
            to = "tests/%s%s" % (name, obj)
            te = "tests/%s%s" % (name, exeext)
            w("build %s: tcc %s/%s" % (to, "$root", t))
            if name in ("time_dst", "stdio_errors"):
                w("  extra = -I" + q(norm(os.path.join(ROOT, "src/internal"))))
            elif name == "threads_timedwait":
                w("  extra = -I" + q(norm(ROOT)))
            inputs = to + (" " + helper if t in uses_next_test else "")
            w("build %s: link %s | %s" % (te, inputs, " ".join(nesc(l) for l in libs)))
            w("  libs = " + " ".join(libs))
            texes.append(te)
    w("build all: phony libs " + " ".join(texes))
    w("default all")

    with open(os.path.join(bdir, "build.ninja"), "w", newline="\n") as f:
        f.write("\n".join(n) + "\n")

    if a.generate_only:
        return 0
    if a.executor == "direct":
        if a.platform != "windows":
            sys.exit("build.py: the direct executor currently verifies Windows COFF builds only; use --executor ninja")
        command = [sys.executable, os.path.join(ROOT, "tools", "verify_direct.py"), bdir]
        if a.jobs:
            command += ["--jobs", str(a.jobs)]
        if a.command != "test":
            command.append("--build-only")
        return run(command).returncode
    ninja = shutil.which("ninja")
    if not ninja:
        sys.exit("build.py: ninja not found in PATH")
    cmd = [ninja, "-C", bdir, "-k", "0"]
    if a.jobs:
        cmd += ["-j", str(a.jobs)]
    cmd += ["all"] if texes else ["libs"]
    r = run(cmd)
    if r.returncode:
        return r.returncode

    if a.command == "test":
        runner = []
        if a.platform == "linux" and os.name == "nt":
            runner = ["wsl"]
        failed = []
        for te in texes:
            path = os.path.join(bdir, te)
            if runner:
                drive, rest = os.path.splitdrive(path)
                path = "/mnt/%s%s" % (drive[0].lower(), norm(rest))
            print("== %s" % te, flush=True)
            p = run(runner + [path])
            if p.returncode:
                failed.append((te, p.returncode))
        print()
        if failed:
            for te, rc in failed:
                print("FAILED: %s (exit %d)" % (te, rc))
            return 1
        print("all %d test programs passed" % len(texes))
    return 0


if __name__ == "__main__":
    sys.exit(main())
