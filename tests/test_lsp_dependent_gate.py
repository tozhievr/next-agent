import pytest

from nextagent.core.changeset import ChangeSet, Edit
from nextagent.lsp.gate import DiagnosticGate
from nextagent.store.db import Store


class DependentClangd:
    def __init__(self, fail=False):
        self.fail = fail
        self.events = []
        self.texts = {}
        self.uri_to_name = {}

    def open_document(self, path, text, version):
        uri = path.as_uri()
        self.uri_to_name[uri] = path.name
        self.texts[uri] = text
        self.events.append(("open", path.name))
        return uri

    def change_document(self, uri, text, version):
        self.texts[uri] = text
        self.events.append(("change", self.uri_to_name[uri]))

    def wait_diagnostics(self, uri, version):
        self.events.append(("diagnostics", self.uri_to_name[uri]))
        changed_header = any(
            self.uri_to_name[key] == "api.h" and "broken" in text
            for key, text in self.texts.items()
        )
        diagnostics = []
        if self.fail and self.uri_to_name[uri] == "main.c" and changed_header:
            diagnostics = [{"severity": 1, "message": "dependent compile error"}]
        return {"version": version, "diagnostics": diagnostics}

    def notify(self, method, params):
        self.events.append(("close", self.uri_to_name[params["textDocument"]["uri"]]))


def test_header_edit_opens_unchanged_transitive_dependent(tmp_path):
    (tmp_path / "api.h").write_text("int api(void);\n", encoding="utf-8")
    (tmp_path / "bridge.h").write_text('#include "api.h"\n', encoding="utf-8")
    (tmp_path / "main.c").write_text('#include "bridge.h"\n', encoding="utf-8")
    client = DependentClangd()
    with Store(tmp_path) as store:
        store.edit(
            {"api.h": ChangeSet((Edit(0, 0, "/* updated */\n"),))},
            validator=DiagnosticGate(tmp_path, client),
        )
    assert ("open", "main.c") in client.events
    assert ("change", "main.c") in client.events
    assert client.events.count(("close", "main.c")) == 1


def test_header_regression_in_unchanged_dependent_rejects_transaction(tmp_path):
    (tmp_path / "api.h").write_text("int api(void);\n", encoding="utf-8")
    (tmp_path / "main.c").write_text('#include "api.h"\n', encoding="utf-8")
    client = DependentClangd(fail=True)
    with Store(tmp_path) as store:
        with pytest.raises(ValueError, match="diagnostic_regression"):
            store.edit(
                {"api.h": ChangeSet((Edit(0, 0, "broken\n"),))},
                validator=DiagnosticGate(tmp_path, client),
            )
        assert store.head() == 0
        assert store.view("api.h").text == "int api(void);\n"
    assert ("close", "main.c") in client.events
