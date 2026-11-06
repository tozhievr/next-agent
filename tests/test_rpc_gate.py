import io
import json

from nextagent.proto.rpc import serve
from nextagent.store.db import Store


def _request(identifier, method, params):
    return {"jsonrpc": "2.0", "id": identifier, "method": method, "params": params}


def test_rpc_rejected_edit_does_not_commit(tmp_path):
    def reject(_changes):
        raise ValueError("diagnostic_regression: new error")

    messages = [
        _request(1, "edit", {"ops": [{"path": "main.c", "start": 0,
                                      "end": 0, "text": "broken"}], "if_rev": 0}),
        _request(2, "open", {}),
        _request(3, "view", {"target": "main.c"}),
    ]
    output = io.StringIO()
    serve(tmp_path, io.StringIO("\n".join(map(json.dumps, messages)) + "\n"),
          output, validator=reject)
    replies = [json.loads(line) for line in output.getvalue().splitlines()]
    assert "diagnostic_regression" in replies[0]["error"]["message"]
    assert replies[1]["result"]["rev"] == 0
    assert replies[2]["result"]["text"] == ""


def test_rpc_validator_applies_to_symbol_replacement(tmp_path):
    (tmp_path / "main.c").write_text("int main(void) { return 0; }", encoding="utf-8")
    calls = []

    def accept(changes):
        calls.append(changes)

    message = _request(1, "replace_symbol", {
        "target": "main.c", "name": "main", "kind": "function",
        "replacement": "int main(void) { return 1; }", "if_rev": 0,
    })
    output = io.StringIO()
    serve(tmp_path, io.StringIO(json.dumps(message) + "\n"), output, validator=accept)
    assert json.loads(output.getvalue())["result"]["ok"]
    assert len(calls) == 1
    with Store(tmp_path) as store:
        assert "return 1;" in store.view("main.c").text
