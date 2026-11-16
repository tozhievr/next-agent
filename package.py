"""Package the native CMake distribution with CPack and SHA-256 checksums."""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument("--out", type=Path, default=ROOT / "dist")
platform = "windows" if os.name == "nt" else "linux"
ap.add_argument("--build", type=Path, default=ROOT / "build" / platform)
a = ap.parse_args()
config = a.build.resolve() / "CPackConfig.cmake"
assert config.is_file(), "Run python build.py first"
subprocess.run(["cpack", "--config", str(config), "-B", str(a.out.resolve())], check=True)
pattern = "nextagent-*-windows-x64.zip" if platform == "windows" else "nextagent-*-linux-x64.tar.gz"
for archive in a.out.glob(pattern):
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    Path(str(archive) + ".sha256").write_text(f"{digest}  {archive.name}\n", encoding="utf-8")
    print(archive.resolve())
