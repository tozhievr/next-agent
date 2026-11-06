import pytest

from nextagent.core.changeset import ChangeSet, Edit
from nextagent.lsp.gate import DiagnosticGate
from nextagent.store.db import Store


class FakeClangd:
    def __init__(self, fail=False, stale=False):
        self.fail = fail
        self.stale = stale
        self.text = ""
        self.events = []

    def open_document(self, path, text, version):
        self.text = text
        self.events.append(("open", version))
        return path.as_uri()

    def change_document(self, uri, text, version):
        self.text = text
        self.events.append(("change", version))

    def wait_diagnostics(self, uri, version):
        if self.stale:
            return {"version": version - 1, "diagnostics": []}
        problems = [{"severity": 1, "message": "new error"}] if (
            self.fail and "broken" in self.text
        ) else []
        return {"version": version, "diagnostics": problems}

    def notify(self, method, params):
        self.events.append((method, params))


def test_gate_rejects_candidate_without_revision_or_disk_write(tmp_path):
    path = tmp_path / "main.c"
    path.write_text("int main(void) { return 0; }", encoding="utf-8")
    client = FakeClangd(fail=True)
    with Store(tmp_path) as store:
        with pytest.raises(ValueError, match="diagnostic_regression"):
            store.edit(
                {"main.c": ChangeSet((Edit(0, 3, "broken"),))},
                validator=DiagnosticGate(tmp_path, client),
            )
        assert store.head() == 0
        assert store.view("main.c").text == path.read_text(encoding="utf-8")
        assert client.events[-1][0] == "textDocument/didClose"


def test_gate_accepts_candidate_and_preserves_disk_until_sync(tmp_path):
    path = tmp_path / "main.c"
    path.write_text("int a;", encoding="utf-8")
    with Store(tmp_path) as store:
        result = store.edit(
            {"main.c": ChangeSet((Edit(4, 5, "b"),))},
            validator=DiagnosticGate(tmp_path, FakeClangd()),
        )
        assert result["ok"]
        assert store.view("main.c").text == "int b;"
        assert path.read_text(encoding="utf-8") == "int a;"


def test_gate_rejects_stale_report(tmp_path):
    with Store(tmp_path) as store:
        with pytest.raises(ValueError, match="diagnostics_unavailable_or_stale"):
            store.edit(
                {"main.c": ChangeSet((Edit(0, 0, "int a;"),))},
                validator=DiagnosticGate(tmp_path, FakeClangd(stale=True)),
            )
        assert store.head() == 0


def test_validator_exception_rolls_back_multi_file_batch(tmp_path):
    with Store(tmp_path) as store:
        def reject(_changes):
            raise ValueError("blocked")
        with pytest.raises(ValueError, match="blocked"):
            store.edit({
                "a.c": ChangeSet((Edit(0, 0, "a"),)),
                "b.c": ChangeSet((Edit(0, 0, "b"),)),
            }, validator=reject)
        assert store.head() == 0
        assert store.view("a.c").text == ""
        assert store.view("b.c").text == ""
