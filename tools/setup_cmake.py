"""Install an official CMake release in build/toolchain; also supports GitHub Actions."""
import os
from pathlib import Path
import platform
import tarfile
import urllib.request
import zipfile

VERSION = "4.5.0-rc1"
ROOT = Path(__file__).resolve().parent.parent / "build/toolchain"
system = "windows" if os.name == "nt" else "linux"
if platform.machine().lower() not in ("amd64", "x86_64"):
    raise SystemExit("This project currently packages x86-64 toolchains only")
name = f"cmake-{VERSION}-{system}-x86_64"
suffix = ".zip" if system == "windows" else ".tar.gz"
binary = ROOT / name / "bin" / ("cmake.exe" if os.name == "nt" else "cmake")
if not binary.is_file():
    ROOT.mkdir(parents=True, exist_ok=True)
    archive = ROOT / (name + suffix)
    url = f"https://github.com/Kitware/CMake/releases/download/v{VERSION}/{archive.name}"
    urllib.request.urlretrieve(url, archive)
    if system == "windows":
        with zipfile.ZipFile(archive) as package:
            for entry in package.namelist():
                if not (ROOT / entry).resolve().is_relative_to(ROOT.resolve()):
                    raise ValueError("Unexpected archive path")
            package.extractall(ROOT)
    else:
        with tarfile.open(archive) as package:
            package.extractall(ROOT, filter="data")
if os.environ.get("GITHUB_PATH"):
    with open(os.environ["GITHUB_PATH"], "a", encoding="utf-8") as output:
        output.write(str(binary.parent) + "\n")
print(binary.parent)
