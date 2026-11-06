from nextagent.core.changeset import ChangeSet, Edit
from nextagent.lsp.dependencies import dependent_sources
from nextagent.store.db import Store


def test_unflushed_new_header_is_discovered_from_store(tmp_path):
    (tmp_path / "main.c").write_text(
        '#include "new.h"\nint main(void) { return api(); }\n',
        encoding="utf-8",
    )
    with Store(tmp_path) as store:
        store.edit({"new.h": ChangeSet((Edit(0, 0, "int api(void);\n"),))})
        assert not (tmp_path / "new.h").exists()
        assert dependent_sources(tmp_path, {"new.h"}) == set()
        assert dependent_sources(
            tmp_path, {"new.h"}, snapshot_text=store.snapshot_text,
        ) == {"main.c"}


def test_unflushed_new_header_transitive_dependency(tmp_path):
    (tmp_path / "main.c").write_text('#include "wrapper.h"\n', encoding="utf-8")
    (tmp_path / "wrapper.h").write_text('#include "new.h"\n', encoding="utf-8")
    with Store(tmp_path) as store:
        store.edit({"new.h": ChangeSet((Edit(0, 0, "int api(void);\n"),))})
        assert dependent_sources(
            tmp_path, {"new.h"}, snapshot_text=store.snapshot_text,
        ) == {"main.c"}
