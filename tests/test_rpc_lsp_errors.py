import io
import json

import pytest

from nextagent.proto.rpc import serve
from nextagent.store.db import Store


@pytest.mark.parametrize("failure", [
    TimeoutError("diagnostics_unavailable_or_stale"),
    RuntimeError("clangd connection closed"),
])
def test_rpc_survives_lsp_validation_failure(tmp_path, failure):
    def reject(_changes):
        raise failure

    requests = [
        {"jsonrpc": "2.0", "id": 1, "method": "edit", "params": {
            "ops": [{"path": "main.c", "start": 0, "end": 0, "text": "int a;"}],
            "if_rev": 0,
        }},
        {"jsonrpc": "2.0", "id": 2, "method": "open", "params": {}},
    ]
    sink = io.StringIO()
    serve(tmp_path, io.StringIO("\n".join(map(json.dumps, requests)) + "\n"),
          sink, validator=reject)
    replies = [json.loads(line) for line in sink.getvalue().splitlines()]
    assert len(replies) == 2
    assert replies[0]["error"]["code"] == -32602
    assert str(failure) in replies[0]["error"]["message"]
    assert replies[1]["result"]["rev"] == 0
    with Store(tmp_path) as store:
        assert store.head() == 0
