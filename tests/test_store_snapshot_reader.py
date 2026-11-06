import pytest

from nextagent.core.changeset import ChangeSet, Edit
from nextagent.store.db import Store


def test_snapshot_text_does_not_start_transaction_or_import_document(tmp_path):
    (tmp_path / "main.c").write_text("int main;\n", encoding="utf-8")
    with Store(tmp_path) as store:
        assert not store.db.in_transaction
        assert store.snapshot_text("main.c") == "int main;\n"
        assert not store.db.in_transaction
        assert store.db.execute("SELECT COUNT(*) FROM doc").fetchone()[0] == 0
        assert store.snapshot_text("missing.c") == ""
        assert not store.db.in_transaction


def test_snapshot_text_reads_unflushed_revision(tmp_path):
    (tmp_path / "main.c").write_text("int main;\n", encoding="utf-8")
    with Store(tmp_path) as store:
        store.edit({"main.c": ChangeSet((Edit(0, 0, "static "),))})
        assert store.snapshot_text("main.c") == "static int main;\n"
        assert not store.db.in_transaction
        assert (tmp_path / "main.c").read_text(encoding="utf-8") == "int main;\n"


def test_snapshot_text_during_validator_preserves_atomic_rollback(tmp_path):
    (tmp_path / "main.c").write_text("int main;\n", encoding="utf-8")
    with Store(tmp_path) as store:
        def reject(_changes):
            assert not store.db.in_transaction
            assert store.snapshot_text("main.c") == "int main;\n"
            assert not store.db.in_transaction
            raise ValueError("rejected")

        with pytest.raises(ValueError, match="rejected"):
            store.edit(
                {"main.c": ChangeSet((Edit(0, 0, "broken "),))},
                validator=reject,
            )
        assert store.head() == 0
        assert not store.db.in_transaction
        assert store.snapshot_text("main.c") == "int main;\n"


def test_snapshot_text_rejects_workspace_escape(tmp_path):
    with Store(tmp_path) as store:
        with pytest.raises(ValueError, match="path escapes workspace"):
            store.snapshot_text("../outside.c")
