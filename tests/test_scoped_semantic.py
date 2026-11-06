import io
import json

import pytest

from nextagent.core.snapshot import Snapshot
from nextagent.proto.rpc import serve
from nextagent.semantic.c23 import outline, select


def test_struct_fields_have_scopes():
    snap = Snapshot.of(1, 0, "struct Point { int x; int y; };\n"
                       "struct Other { int x; };\n")
    fields = [s for s in outline(snap, nested=True) if s.kind == "field"]
    assert {(s.scope, s.name) for s in fields} == {
        ("Point", "x"), ("Point", "y"), ("Other", "x")
    }
    assert select(snap, "x", "field", "Point").scope == "Point"
    with pytest.raises(ValueError, match="ambiguous_symbol"):
        select(snap, "x", "field")


def test_rpc_symbol_edit_checks_workspace_revision(tmp_path):
    path = tmp_path / "main.c"
    path.write_text("int main(void) { return 0; }\n", encoding="utf-8")
    requests = [
        {"jsonrpc": "2.0", "id": 1, "method": "replace_symbol", "params": {
            "target": "main.c", "name": "main", "kind": "function",
            "replacement": "int main(void) { return 42; }", "if_rev": 0}},
        {"jsonrpc": "2.0", "id": 2, "method": "replace_symbol", "params": {
            "target": "main.c", "name": "main", "kind": "function",
            "replacement": "int main(void) { return 2; }", "if_rev": 0}},
        {"jsonrpc": "2.0", "id": 3, "method": "view", "params": {"target": "main.c"}},
    ]
    sink = io.StringIO()
    serve(tmp_path, io.StringIO("\n".join(map(json.dumps, requests)) + "\n"), sink)
    replies = [json.loads(line) for line in sink.getvalue().splitlines()]
    assert replies[0]["result"]["ok"] is True
    assert "stale_rev" in replies[1]["error"]["message"]
    assert "return 42;" in replies[2]["result"]["text"]
    assert path.read_text(encoding="utf-8") == "int main(void) { return 0; }\n"
