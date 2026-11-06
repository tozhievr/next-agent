import io
import json

import pytest

from nextagent.core.changeset import ChangeSet, Edit
from nextagent.proto.rpc import serve
from nextagent.store.db import Store


def test_guarded_undo_rejection_preserves_revision(tmp_path):
    with Store(tmp_path) as store:
        store.edit({"main.c": ChangeSet((Edit(0, 0, "int a;"),))})
        head = store.head()

        def reject(_changes):
            raise ValueError("diagnostic_regression: undo rejected")

        with pytest.raises(ValueError, match="undo rejected"):
            store.undo(validator=reject)
        assert store.head() == head
        assert store.view("main.c").text == "int a;"


def test_guarded_undo_success(tmp_path):
    with Store(tmp_path) as store:
        store.edit({"main.c": ChangeSet((Edit(0, 0, "int a;"),))})
        calls = []

        def accept(changes):
            calls.append(changes)

        result = store.undo(validator=accept)
        assert result["ok"]
        assert store.view("main.c").text == ""
        assert len(calls) == 1


def test_rpc_history_undo_uses_validator(tmp_path):
    with Store(tmp_path) as store:
        store.edit({"main.c": ChangeSet((Edit(0, 0, "int a;"),))})
        head = store.head()

    request = {"jsonrpc": "2.0", "id": 1, "method": "history",
               "params": {"op": "undo"}}

    def reject(_changes):
        raise ValueError("diagnostic_regression")

    sink = io.StringIO()
    serve(tmp_path, io.StringIO(json.dumps(request) + "\n"), sink, validator=reject)
    assert "diagnostic_regression" in json.loads(sink.getvalue())["error"]["message"]
    with Store(tmp_path) as store:
        assert store.head() == head
        assert store.view("main.c").text == "int a;"
