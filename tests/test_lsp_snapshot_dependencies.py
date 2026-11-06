from nextagent.core.changeset import ChangeSet, Edit
from nextagent.lsp.dependencies import dependent_sources
from nextagent.lsp.gate import DiagnosticGate
from nextagent.store.db import Store


class CaptureClient:
    def __init__(self):
        self.opened = []

    def open_document(self, path, text, version):
        self.opened.append(path.name)
        return path.as_uri()

    def change_document(self, uri, text, version):
        pass

    def wait_diagnostics(self, uri, version):
        return {"version": version, "diagnostics": []}

    def notify(self, method, params):
        pass


def test_unsynced_include_graph_from_store_snapshots(tmp_path):
    (tmp_path / "api.h").write_text("int api(void);\n", encoding="utf-8")
    (tmp_path / "main.c").write_text("int main(void) { return 0; }\n", encoding="utf-8")
    with Store(tmp_path) as store:
        store.edit({
            "main.c": ChangeSet((Edit(0, 0, '#include "api.h"\n'),))
        })
        assert dependent_sources(tmp_path, {"api.h"}) == set()
        snapshot = store.snapshot_text
        assert dependent_sources(tmp_path, {"api.h"}, snapshot_text=snapshot) == {"main.c"}
        client = CaptureClient()
        store.edit(
            {"api.h": ChangeSet((Edit(0, 0, "/* updated */\n"),))},
            validator=DiagnosticGate(tmp_path, client, snapshot_text=snapshot),
        )
        assert "main.c" in client.opened


def test_dependency_snapshot_removes_obsolete_disk_include(tmp_path):
    (tmp_path / "api.h").write_text("int api(void);\n", encoding="utf-8")
    original = '#include "api.h"\nint main(void) { return 0; }\n'
    (tmp_path / "main.c").write_text(original, encoding="utf-8")
    with Store(tmp_path) as store:
        store.edit({
            "main.c": ChangeSet((Edit(0, len('#include "api.h"\n'), ""),))
        })
        assert dependent_sources(tmp_path, {"api.h"}) == {"main.c"}
        assert dependent_sources(
            tmp_path, {"api.h"}, snapshot_text=store.snapshot_text
        ) == set()
