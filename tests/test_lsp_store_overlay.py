from nextagent.core.changeset import ChangeSet, Edit
from nextagent.lsp.gate import DiagnosticGate
from nextagent.store.db import Store


class RecordingClangd:
    def __init__(self):
        self.opened = {}

    def open_document(self, path, text, version):
        self.opened[path.name] = text
        return path.as_uri()

    def change_document(self, uri, text, version):
        pass

    def wait_diagnostics(self, uri, version):
        return {"version": version, "diagnostics": []}

    def notify(self, method, params):
        pass


def test_dependent_uses_unflushed_store_snapshot(tmp_path):
    (tmp_path / "api.h").write_text("int api(void);\n", encoding="utf-8")
    (tmp_path / "main.c").write_text('#include "api.h"\nint a;\n', encoding="utf-8")
    with Store(tmp_path) as store:
        original = store.view("main.c").text
        store.edit({
            "main.c": ChangeSet((Edit(len(original), len(original), "int b;\n"),))
        })
        assert (tmp_path / "main.c").read_text(encoding="utf-8") == original
        client = RecordingClangd()
        gate = DiagnosticGate(
            tmp_path, client, snapshot_text=store.snapshot_text
        )
        store.edit({
            "api.h": ChangeSet((Edit(0, 0, "/* changed */\n"),))
        }, validator=gate)
        assert client.opened["main.c"] == original + "int b;\n"
        assert (tmp_path / "main.c").read_text(encoding="utf-8") == original
