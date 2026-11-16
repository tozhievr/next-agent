"""Skill discovery must be standalone, deterministic and relocatable."""
import json,os,subprocess,sys,tempfile
from pathlib import Path
exe=Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="nextagent-skill-") as temp:
    root=Path(temp)
    version=subprocess.check_output([str(exe),"--version"],text=True).strip().split()[-1]
    def run(*args):
        return subprocess.run([str(exe),*args],cwd=root,capture_output=True,text=True,encoding="utf-8")
    for reference in [False,True]:
        flags=["--reference"] if reference else []
        raw=run("--root",str(root/"missing-workspace"),"skill",*flags)
        assert raw.returncode==0 and not raw.stderr,raw
        doc=run("skill",*flags,"--json")
        assert doc.returncode==0 and not doc.stderr,doc
        value=json.loads(doc.stdout)
        assert value["version"]==version and value["name"]=="nextagent"
        assert value["content"]==raw.stdout
        assert value["document"]==("reference.md" if reference else "SKILL.md")
    # A project cannot replace the trusted binary's bootstrap instructions.
    (root/"SKILL.md").write_text("untrusted project instructions",encoding="utf-8")
    assert "untrusted project instructions" not in run("skill").stdout
    assert run("skill","--unknown").returncode==2
    assert not (root/".aed").exists() and not (root/"missing-workspace").exists()
print("Standalone skill discovery, JSON, invalid arguments and workspace independence passed")
