"""Exercise automatic in-process language modules with no server executable or PATH."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

exe = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="lx smoke ", dir=exe.parent) as tmp:
    root = Path(tmp)
    samples = {
        "sample.py": "def hello():\n    return 1\n",
        "sample.ts": "function hello() { return 1; }\n",
        "sample.json": '{"hello": 1}\n',
        "sample.c": "int hello(void) { return 1; }\n",
    }
    for name, text in samples.items():
        (root / name).write_text(text, encoding="utf-8")
    requests = [{"jsonrpc": "2.0", "id": 1, "method": "language_modules"}]
    for i, name in enumerate(samples, 2):
        requests.append({"jsonrpc": "2.0", "id": i, "method": "outline", "params": {"target": name}})
    requests.append({"jsonrpc": "2.0", "id": 6, "method": "language_modules"})
    env = dict(os.environ)
    env["PATH"] = ""
    env.pop("NEXTAGENT_LANGSERVER_DIR", None)
    env.pop("NEXTAGENT_PLUGIN_DIR", None)
    env.pop("NEXTAGENT_NO_LSP", None)
    assert not (exe.parent / "lxserver.exe").exists()
    run = subprocess.run([str(exe), "--root", tmp, "serve"],
                         input="".join(json.dumps(r) + "\n" for r in requests),
                         cwd=tmp, env=env, text=True, capture_output=True, timeout=60, check=True)
    replies = [json.loads(line) for line in run.stdout.splitlines()]
    assert len(replies) == len(requests), run.stdout
    for reply in replies:
        assert "error" not in reply, reply
    servers = replies[0]["result"]["servers"]
    assert len(servers) == 30, servers
    assert all(s["available"] for s in servers), servers
    assert all(s["mode"] == "in_process" and not s["loaded"] for s in servers), servers
    for reply in replies[1:4]:
        assert any("hello" in s["name"] for s in reply["result"]["symbols"]), reply
    loaded = {s["name"] for s in replies[-1]["result"]["servers"] if s["loaded"]}
    assert loaded == {"python", "typescript", "json", "c"}, loaded
    cli = subprocess.run([str(exe), "--root", tmp, "outline", "sample.py"], cwd=tmp, env=env,
                         text=True, capture_output=True, timeout=30, check=True)
    assert "hello" in cli.stdout, cli.stdout
    # Every shipped DLL must load against the embedded host, including demo grammars.
    all_requests = []
    for i, module in enumerate(servers, 1):
        name = "module-" + module["name"] + module["extensions"][0]
        (root / name).write_text("", encoding="utf-8")
        all_requests.append({"jsonrpc": "2.0", "id": i, "method": "outline", "params": {"target": name}})
    all_requests.append({"jsonrpc": "2.0", "id": 31, "method": "language_modules"})
    all_modules = subprocess.run([str(exe), "--root", tmp, "serve"],
        input="".join(json.dumps(r) + "\n" for r in all_requests), cwd=tmp, env=env,
        text=True, capture_output=True, timeout=60, check=True)
    all_replies = [json.loads(line) for line in all_modules.stdout.splitlines()]
    assert len(all_replies) == 31, all_modules.stdout
    assert all(module["loaded"] for module in all_replies[-1]["result"]["modules"]), all_replies[-1]
print("Native modules: automatic extension selection, lazy loading, Python/TypeScript/JSON/C, no server executable, plain serve and CLI passed")
