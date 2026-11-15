"""Development file adapter: all source reads/changes pass through NextAgent."""
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent.parent
class AgentIO:
    def __init__(self, root=ROOT):
        self.process = subprocess.Popen([str(ROOT / "build/windows/nextagent.exe"), "--root", str(root), "serve", "--no-gate"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, encoding="utf-8",
            env=dict(os.environ, NEXTAGENT_NO_LSP="1"))
        self.next_id = 0
    def call(self, method, params=None):
        self.next_id += 1
        request = {"jsonrpc":"2.0", "id":self.next_id, "method":method, "params":params or {}}
        self.process.stdin.write(json.dumps(request, ensure_ascii=False)+"\n")
        self.process.stdin.flush()
        reply = json.loads(self.process.stdout.readline())
        if "error" in reply: raise RuntimeError(reply["error"])
        return reply["result"]
    def read(self, path): return self.call("view", {"target":str(path)})["text"]
    def write(self, path, text, label="Harness implementation"):
        try: old = self.read(path)
        except RuntimeError: old = None
        if old == text: return
        if old is None:
            return self.call("patch", {"edits":[{"path":str(path),"new":text,"create":True}], "label":label})
        return self.call("edit", {"ops":[{"path":str(path),"start":0,"end":len(old),"text":text}], "label":label})
    def tree(self, path="", recursive=True):
        result = self.call("tree", {"path":path,"recursive":recursive,"limit":20000})
        if result["truncated"]: raise RuntimeError("Tree limit exceeded")
        return result["entries"]
    def close(self):
        self.process.stdin.close()
        self.process.wait(timeout=30)
    def __enter__(self): return self
    def __exit__(self,*args): self.close()
