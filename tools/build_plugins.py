"""Build native plugins from the sibling next-langserver grammar projects.

python tools/build_plugins.py --source ../next-langserver --cc clang-23
The resulting platform bundle is consumed by the main CMake build and CPack.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source", type=Path, default=ROOT.parent / "next-langserver")
parser.add_argument("--cc", default=os.environ.get("CLANG", "clang"))
parser.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
args = parser.parse_args()
platform = "windows" if os.name == "nt" else "linux"
canonical = ROOT / "vendor/next-langserver/windows-x64/plugins"
manifests = {json.loads(p.read_text(encoding="utf-8"))["id"]: p
             for p in canonical.glob("*.lxmanifest.json")}
sources = {}
for manifest in (args.source / "grammars").rglob("*.lxmanifest.json"):
    lang = json.loads(manifest.read_text(encoding="utf-8"))["id"]
    if lang not in manifests or lang in sources:
        continue
    cmake = manifest.parent / "CMakeLists.txt"
    if not cmake.is_file():
        continue
    match = re.search(r"add_library\(" + re.escape(lang) + r"\s+SHARED\s+([^)]*)\)",
                      cmake.read_text(encoding="utf-8"), re.IGNORECASE)
    if match:
        files = [(manifest.parent / name.strip('"')).resolve() for name in match.group(1).split()]
        if all(p.is_file() for p in files):
            sources[lang] = files
if sources.keys() != manifests.keys():
    parser.error("Missing grammar sources: " + ", ".join(sorted(manifests.keys() - sources.keys())))

project = ROOT / f"build/plugin-{platform}-source"
build = ROOT / f"build/plugin-{platform}"
project.mkdir(parents=True, exist_ok=True)
lines = ["cmake_minimum_required(VERSION 4.5)", "project(NextAgentPlugins LANGUAGES C)",
         "set(CMAKE_C_STANDARD 99)", "set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)",
         'include_directories("' + (ROOT / "vendor/next-langserver/source/include").as_posix() + '")']
for lang, files in sorted(sources.items()):
    lines.append(f"add_library({lang} SHARED " + " ".join('"' + p.as_posix() + '"' for p in files) + ")")
(project / "CMakeLists.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
subprocess.run(["cmake", "-S", str(project), "-B", str(build), "-G", "Ninja",
                "-DCMAKE_C_COMPILER=" + args.cc, "-DCMAKE_BUILD_TYPE=Release"], check=True)
subprocess.run(["cmake", "--build", str(build), "--parallel", str(args.jobs)], check=True)

destination = ROOT / f"vendor/next-langserver/{platform}-x64/plugins"
destination.mkdir(parents=True, exist_ok=True)
for lang, manifest in sorted(manifests.items()):
    library = lang + ".dll" if platform == "windows" else "lib" + lang + ".so"
    shutil.copy2(build / library, destination / library)
    data = json.loads(manifest.read_text(encoding="utf-8"))
    data["library"] = library
    if destination != canonical:
        (destination / manifest.name).write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        queries = data.get("queries_dir", lang + ".queries")
        if (canonical / queries).is_dir():
            shutil.copytree(canonical / queries, destination / queries, dirs_exist_ok=True)

vendor = ROOT / "vendor/next-langserver"
files = sorted(p for folder in ("source", "windows-x64", "linux-x64")
               for p in (vendor / folder).rglob("*") if p.is_file())
(vendor / "SHA256SUMS.txt").write_text("".join(
    hashlib.sha256(p.read_bytes()).hexdigest() + "  " + p.relative_to(vendor).as_posix() + "\n"
    for p in files), encoding="utf-8")
print(f"Built {len(sources)} native {platform} plugins: {destination}")
