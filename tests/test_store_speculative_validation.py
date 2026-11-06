import pytest

from nextagent.core.changeset import ChangeSet, Edit
from nextagent.store.db import Store


def test_validator_runs_without_sqlite_write_transaction(tmp_path):
    (tmp_path / "main.c").write_text("int main;\n", encoding="utf-8")
    with Store(tmp_path) as store:
        seen = []

        def validate(changes):
            seen.append(not store.db.in_transaction)
            assert changes["main.c"][0].text == "int main;\n"
            assert changes["main.c"][1].text == "static int main;\n"

        store.edit(
            {"main.c": ChangeSet((Edit(0, 0, "static "),))},
            validator=validate,
        )
        assert seen == [True]
        assert store.snapshot_text("main.c") == "static int main;\n"


def test_concurrent_revision_during_validation_fails_closed(tmp_path):
    (tmp_path / "main.c").write_text("int main;\n", encoding="utf-8")
    with Store(tmp_path) as first, Store(tmp_path) as second:
        def validate(_changes):
            assert not first.db.in_transaction
            second.edit({"main.c": ChangeSet((Edit(0, 0, "extern "),))})

        with pytest.raises(ValueError, match="stale_rev"):
            first.edit(
                {"main.c": ChangeSet((Edit(0, 0, "static "),))},
                validator=validate,
            )
        assert first.head() == 1
        assert first.snapshot_text("main.c") == "extern int main;\n"


def test_disk_change_during_validation_fails_closed(tmp_path):
    path = tmp_path / "main.c"
    path.write_text("int main;\n", encoding="utf-8")
    with Store(tmp_path) as store:
        def validate(_changes):
            path.write_text("int changed;\n", encoding="utf-8")

        with pytest.raises(ValueError, match="stale_snapshot"):
            store.edit(
                {"main.c": ChangeSet((Edit(0, 0, "static "),))},
                validator=validate,
            )
        assert store.head() == 0
        assert store.snapshot_text("main.c") == "int changed;\n"


def test_validator_rejection_does_not_open_transaction(tmp_path):
    with Store(tmp_path) as store:
        def reject(_changes):
            assert not store.db.in_transaction
            raise ValueError("rejected")

        with pytest.raises(ValueError, match="rejected"):
            store.edit(
                {"new.c": ChangeSet((Edit(0, 0, "int a;\n"),))},
                validator=reject,
            )
        assert not store.db.in_transaction
        assert store.head() == 0
