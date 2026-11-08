#!/usr/bin/env python3
"""Builds the NextAgent skill package: nextagent-skill-<version>.zip (+ .sha256) in the given directory.

usage: python skill/package.py [OUT_DIR]        (default: build/skill)

The archive holds one folder, nextagent-skill-<version>/, with the skill itself (nextagent/SKILL.md, reference.md,
VERSION), install-skill.sh, install-skill.ps1, README.md and LICENSE. The build is reproducible: fixed timestamps,
sorted entries, no compression (zlib output differs between Python builds, and the package is 20 KB). SKILL.md is validated first (front matter with name and description)."""
import hashlib, os, re, sys, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKILL = os.path.join(ROOT, "skill")


def read(path):
    with open(path, "rb") as f:
        return f.read()


def version():
    m = re.search(rb'#define NA_VERSION "([^"]+)"', read(os.path.join(ROOT, "src", "util.h")))
    if not m:
        sys.exit("package.py: NA_VERSION not found in src/util.h")
    return m.group(1).decode()


def validate(skill_md):
    text = skill_md.decode("utf-8").replace("\r\n", "\n")
    m = re.match(r"---\n(.*?)\n---\n", text, re.S)
    if not m:
        sys.exit("package.py: SKILL.md has no front matter")
    fields = dict(re.findall(r"^([a-z-]+):\s*(.+)$", m.group(1), re.M))
    if fields.get("name") != "nextagent":
        sys.exit("package.py: SKILL.md front matter needs name: nextagent")
    if len(fields.get("description", "")) < 40:
        sys.exit("package.py: SKILL.md front matter needs a description")
    for name in re.findall(r"`([A-Za-z0-9_.-]+\.md)`", text):
        if name != "SKILL.md" and not os.path.exists(os.path.join(SKILL, "nextagent", name)):
            sys.exit("package.py: SKILL.md refers to %s which is not in the package" % name)


def lf(data):  # text files are shipped with LF endings whatever the checkout did
    return data.replace(b"\r\n", b"\n")


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "skill")
    ver = version()
    skill_md = lf(read(os.path.join(SKILL, "nextagent", "SKILL.md")))
    validate(skill_md)
    top = "nextagent-skill-%s" % ver
    files = {
        "nextagent/SKILL.md": (skill_md, 0o644),
        "nextagent/reference.md": (lf(read(os.path.join(SKILL, "nextagent", "reference.md"))), 0o644),
        "nextagent/VERSION": (("%s\n" % ver).encode(), 0o644),
        "install-skill.sh": (lf(read(os.path.join(SKILL, "install-skill.sh"))), 0o755),
        "install-skill.ps1": (lf(read(os.path.join(SKILL, "install-skill.ps1"))), 0o644),
        "README.md": (lf(read(os.path.join(SKILL, "README.md"))), 0o644),
        "LICENSE": (lf(read(os.path.join(ROOT, "LICENSE"))), 0o644),
    }
    os.makedirs(out, exist_ok=True)
    zpath = os.path.join(out, top + ".zip")
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_STORED) as z:
        for name in sorted(files):
            data, mode = files[name]
            zi = zipfile.ZipInfo("%s/%s" % (top, name), date_time=(2020, 1, 1, 0, 0, 0))
            zi.external_attr = (0o100000 | mode) << 16
            zi.create_system = 3  # Unix, whatever platform builds it
            zi.compress_type = zipfile.ZIP_STORED
            z.writestr(zi, data)
    digest = hashlib.sha256(read(zpath)).hexdigest()
    with open(zpath + ".sha256", "w") as f:
        f.write("%s  %s\n" % (digest, os.path.basename(zpath)))
    print("%s (%d files, sha256 %s)" % (zpath, len(files), digest[:12]))


main()
