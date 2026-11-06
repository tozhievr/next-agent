import pytest

from nextagent.core.changeset import ChangeSet, Edit
from nextagent.lsp.gate import DiagnosticGate
from nextagent.store.db import Store


class BatchClangd:
    def __init__(self, reject=False):
        self.events = []
        self.texts = {}
        self.reject = reject

    def open_document(self, path, text, version):
        uri = path.as_uri()
        self.texts[uri] = text
        self.events.append(("open", uri, version))
        return uri

    def change_document(self, uri, text, version):
        self.texts[uri] = text
        self.events.append(("change", uri, version))

    def wait_diagnostics(self, uri, version):
        self.events.append(("diagnostics", uri, version))
        diagnostics = []
        if self.reject and "broken" in self.texts[uri]:
            diagnostics = [{"severity": 1, "message": "broken"}]
        return {"version": version, "diagnostics": diagnostics}

    def notify(self, method, params):
        self.events.append(("close", params["textDocument"]["uri"], None))


def test_batch_stages_all_documents_before_candidate_diagnostics(tmp_path):
    client = BatchClangd()
    with Store(tmp_path) as store:
        store.edit({
            "a.c": ChangeSet((Edit(0, 0, "int a;"),)),
            "b.c": ChangeSet((Edit(0, 0, "int b;"),)),
        }, validator=DiagnosticGate(tmp_path, client))
        assert store.head() > 0
    events = [event[0] for event in client.events]
    assert events == [
        "open", "open", "diagnostics", "diagnostics",
        "change", "change", "diagnostics", "diagnostics", "close", "close",
    ]


def test_batch_rejects_one_file_without_committing_any(tmp_path):
    client = BatchClangd(reject=True)
    with Store(tmp_path) as store:
        with pytest.raises(ValueError, match="diagnostic_regression"):
            store.edit({
                "a.c": ChangeSet((Edit(0, 0, "int a;"),)),
                "b.c": ChangeSet((Edit(0, 0, "broken"),)),
            }, validator=DiagnosticGate(tmp_path, client))
        assert store.head() == 0
        assert store.view("a.c").text == ""
        assert store.view("b.c").text == ""
    assert sum(event[0] == "close" for event in client.events) == 2
