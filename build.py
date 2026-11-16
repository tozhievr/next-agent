#!/usr/bin/env python3
"""Configure/build/test NextAgent with CMake 4.5 and Clang 23.1.3, C++26."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument("command", nargs="?", default="build", choices=["build", "test", "clean"])
ap.add_argument("--target", choices=["windows", "linux"], default="windows" if os.name == "nt" else "linux")
ap.add_argument("--cc", default=os.environ.get("CLANG", "clang"))
ap.add_argument("--cxx", default=os.environ.get("CXX", "clang++"))
ap.add_argument("--config", choices=["release", "debug"], default="release")
ap.add_argument("--out", type=Path)
ap.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
a = ap.parse_args()
out = (a.out or ROOT / "build" / a.target).resolve()
def run(args):
    subprocess.run(args, check=True, cwd=ROOT)
if a.command == "clean":
    run(["cmake", "--build", str(out), "--target", "clean"])
else:
    if (a.target == "windows") != (os.name == "nt"):
        ap.error("Use a native host or configure CMake with an explicit cross-compilation toolchain")
    run(["cmake", "-S", str(ROOT), "-B", str(out), "-G", "Ninja",
         "-DCMAKE_C_COMPILER=" + a.cc, "-DCMAKE_CXX_COMPILER=" + a.cxx,
         "-DCMAKE_BUILD_TYPE=" + a.config.capitalize()])
    run(["cmake", "--build", str(out), "--parallel", str(a.jobs)])
    if a.command == "test":
        run(["ctest", "--test-dir", str(out), "--output-on-failure"])
