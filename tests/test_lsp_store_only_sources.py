from nextagent.core.changeset import ChangeSet, Edit
from nextagent.lsp.dependencies import dependent_sources
from nextagent.store.db import Store


def test_store_only_source_depends_on_existing_header(tmp_path):
    (tmp_path / "api.h").write_text("int api(void);\n", encoding="utf-8")
    with Store(tmp_path) as store:
        store.edit({
            "new.c": ChangeSet((
                Edit(0, 0, '#include "api.h"\nint main(void) { return api(); }\n'),
            )),
        })
        assert not (tmp_path / "new.c").exists()
        assert store.snapshot_paths() == ("new.c",)
        assert dependent_sources(tmp_path, {"api.h"}) == set()
        assert dependent_sources(
            tmp_path, {"api.h"},
            snapshot_text=store.snapshot_text,
            snapshot_paths=store.snapshot_paths(),
        ) == {"new.c"}


def test_store_only_source_and_header_transitive(tmp_path):
    (tmp_path / "api.h").write_text("int api(void);\n", encoding="utf-8")
    with Store(tmp_path) as store:
        store.edit({
            "new.c": ChangeSet((
                Edit(0, 0, '#include "wrapper.h"\nint main(void) { return api(); }\n'),
            )),
            "wrapper.h": ChangeSet((
                Edit(0, 0, '#include "api.h"\n'),
            )),
        })
        assert dependent_sources(
            tmp_path, {"api.h"},
            snapshot_text=store.snapshot_text,
            snapshot_paths=store.snapshot_paths(),
        ) == {"new.c"}


def test_snapshot_paths_read_only(tmp_path):
    with Store(tmp_path) as store:
        assert store.snapshot_paths() == ()
        assert not store.db.in_transaction
        store.edit({"a.c": ChangeSet((Edit(0, 0, "int a;\n"),))})
        assert store.snapshot_paths() == ("a.c",)
        assert not store.db.in_transaction
