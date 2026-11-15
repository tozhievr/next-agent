"""Validate and run a packaged Windows ZIP or Linux tar.gz after relocation."""
import hashlib
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parent.parent
archive = Path(sys.argv[1]).resolve()
checksum = Path(str(archive) + ".sha256")
if checksum.is_file():
    assert hashlib.sha256(archive.read_bytes()).hexdigest() == checksum.read_text().split()[0]
with tempfile.TemporaryDirectory(prefix="nextagent package ") as tmp:
    destination = Path(tmp).resolve()
    if archive.name.endswith(".zip"):
        with zipfile.ZipFile(archive) as package:
            for name in package.namelist():
                assert (destination / name).resolve().is_relative_to(destination)
            package.extractall(destination)
        executables = list(destination.rglob("nextagent.exe"))
        extension = ".dll"
    else:
        with tarfile.open(archive) as package:
            package.extractall(destination, filter="data")
        executables = list(destination.rglob("nextagent"))
        extension = ".so"
    assert len(executables) == 1, executables
    exe = executables[0]
    plugins = exe.parent / "plugins"
    assert len(list(plugins.glob("*.lxmanifest.json"))) == 30
    assert len(list(plugins.glob("*" + extension))) == 30
    assert not list(exe.parent.rglob("lxserver.exe"))
    for script in ("bundled_lsp.py", "parity.py"):
        subprocess.run([sys.executable, str(ROOT / "tests" / script), str(exe)], check=True)
print("Packaged binary, all 30 native plugins, parity and SHA-256 passed after relocation")
