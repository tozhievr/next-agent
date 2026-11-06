"""JSON-RPC and CLI entrypoint contract tests."""
import io
import json
import subprocess
import sys

from nextagent.proto.rpc import serve


def test_rpc_roundtrip_and_notifications(tmp_path):
    requests = [
        {"jsonrpc": "2.0", "id": 1, "method": "open"},
        {"jsonrpc": "2.0", "id": 2, "method": "edit", "params": {
            "if_rev": 0, "ops": [{"path": "sample.c", "start": 0, "end": 0,
                                  "text": "int main(void) { return 0; }"}]}},
        {"jsonrpc": "2.0", "method": "view", "params": {"target": "sample.c"}},
        {"jsonrpc": "2.0", "id": 3, "method": "view", "params": {"target": "sample.c"}},
        {"jsonrpc": "2.0", "id": 4, "method": "sync"},
        {"jsonrpc": "2.0", "id": 5, "method": "history"},
    ]
    output = io.StringIO()
    serve(tmp_path, io.StringIO("\n".join(json.dumps(x) for x in requests) + "\n"), output)
    responses = [json.loads(x) for x in output.getvalue().splitlines()]
    assert [x["id"] for x in responses] == [1, 2, 3, 4, 5]
    assert responses[0]["result"]["name"] == "NextAgent"
    assert responses[2]["result"]["text"].startswith("int main")
    assert (tmp_path / "sample.c").exists()
    assert len(responses[4]["result"]) == 1


def test_rpc_errors_and_stale_revision(tmp_path):
    lines = [
        "{bad",
        json.dumps({"jsonrpc": "2.0", "id": 1, "method": "missing"}),
        json.dumps({"jsonrpc": "2.0", "id": 2, "method": "view", "params": []}),
        json.dumps({"jsonrpc": "2.0", "id": 3, "method": "edit",
                    "params": {"if_rev": 5, "ops": [
                        {"path": "a.c", "start": 0, "end": 0, "text": "x"}]}}),
    ]
    out = io.StringIO()
    serve(tmp_path, io.StringIO("\n".join(lines) + "\n"), out)
    errors = [json.loads(x)["error"]["code"] for x in out.getvalue().splitlines()]
    assert errors == [-32700, -32601, -32602, -32602]


def test_nextagent_module_entrypoint():
    process = subprocess.run(
        [sys.executable, "-m", "nextagent", "--version"],
        capture_output=True, text=True, check=True,
    )
    assert process.stdout.strip() == "NextAgent 0.2.0"
