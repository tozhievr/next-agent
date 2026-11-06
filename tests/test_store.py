from pathlib import Path

import pytest

from nextagent.core.changeset import ChangeSet, Edit
from nextagent.store.db import Store


def test_atomic_durable_batch_undo_and_sync(tmp_path: Path):
    (tmp_path / "a.c").write_text("alpha\n", encoding="utf-8")
    with Store(tmp_path) as store:
        result = store.edit(
            {
                "a.c": ChangeSet((Edit(0, 5, "beta"),)),
                "b.c": ChangeSet((Edit(0, 0, "new\n"),)),
            },
            if_rev=0,
            label="batch",
        )
        assert result["ok"]
        assert store.view("a.c").text == "beta\n"
        assert (tmp_path / "a.c").read_text() == "alpha\n"
        with pytest.raises(ValueError, match="stale_rev"):
            store.edit({"a.c": ChangeSet((Edit(0, 1, "x"),))}, if_rev=0)
        assert store.view("a.c").text == "beta\n"
        store.undo()
        assert store.view("a.c").text == "alpha\n"
        assert store.view("b.c").text == ""
        assert len(store.history()) == 2
        store.flush(["a.c"])
    with Store(tmp_path) as reopened:
        assert reopened.view("a.c").text == "alpha\n"
        assert len(reopened.history()) == 2


def test_failure_has_no_partial_revision(tmp_path: Path):
    with Store(tmp_path) as store:
        with pytest.raises(ValueError):
            store.edit({
                "ok.c": ChangeSet((Edit(0, 0, "ok"),)),
                "bad.c": ChangeSet((Edit(10, 12, "bad"),)),
            })
        assert store.head() == 0
        assert store.view("ok.c").text == ""


def test_path_security(tmp_path: Path):
    with Store(tmp_path) as store:
        with pytest.raises(ValueError, match="escapes workspace"):
            store.view("../outside.c")
        with pytest.raises(ValueError, match="reserved"):
            store.view(".git/config")
        with pytest.raises(ValueError, match="reserved"):
            store.view(".aed/state.sqlite3")


def test_view_then_edit_same_connection(tmp_path: Path):
    (tmp_path / "source.c").write_text("int n;", encoding="utf-8")
    with Store(tmp_path) as store:
        assert store.view("source.c").text == "int n;"
        assert store.edit({"source.c": ChangeSet((Edit(4, 5, "value"),))})["ok"]
        assert store.view("source.c").text == "int value;"


def test_undo_twice_is_inverse_of_inverse(tmp_path: Path):
    with Store(tmp_path) as store:
        store.edit({"a.c": ChangeSet((Edit(0, 0, "one"),))})
        store.edit({"a.c": ChangeSet((Edit(3, 3, " two"),))})
        assert store.view("a.c").text == "one two"
        store.undo()
        assert store.view("a.c").text == "one"
        store.undo()
        assert store.view("a.c").text == "one two"


def test_reject_canonical_duplicate_paths(tmp_path: Path):
    with Store(tmp_path) as store:
        with pytest.raises(ValueError, match="duplicate canonical"):
            store.edit({
                "x.c": ChangeSet((Edit(0, 0, "a"),)),
                "./x.c": ChangeSet((Edit(0, 0, "b"),)),
            })


def test_sync_refuses_external_disk_modifications(tmp_path: Path):
    path = tmp_path / "source.c"
    path.write_text("before", encoding="utf-8")
    with Store(tmp_path) as store:
        store.edit({"source.c": ChangeSet((Edit(0, 6, "buffer"),))})
        path.write_text("external", encoding="utf-8")
        with pytest.raises(ValueError, match="disk_conflict"):
            store.flush()
        assert path.read_text(encoding="utf-8") == "external"
        assert store.view("source.c").text == "buffer"


def test_sync_rechecks_all_paths_before_writing(tmp_path: Path):
    a = tmp_path / "a.c"
    b = tmp_path / "b.c"
    a.write_text("a", encoding="utf-8")
    b.write_text("b", encoding="utf-8")
    with Store(tmp_path) as store:
        store.edit({
            "a.c": ChangeSet((Edit(0, 1, "A"),)),
            "b.c": ChangeSet((Edit(0, 1, "B"),)),
        })
        b.write_text("external", encoding="utf-8")
        with pytest.raises(ValueError, match="disk_conflict"):
            store.flush()
        assert a.read_text(encoding="utf-8") == "a"
        assert b.read_text(encoding="utf-8") == "external"


def test_sync_updates_baseline_after_success(tmp_path: Path):
    path = tmp_path / "file.c"
    path.write_text("one", encoding="utf-8")
    with Store(tmp_path) as store:
        store.edit({"file.c": ChangeSet((Edit(0, 3, "two"),))})
        store.flush()
        store.edit({"file.c": ChangeSet((Edit(0, 3, "three"),))})
        store.flush()
        assert path.read_text(encoding="utf-8") == "three"
