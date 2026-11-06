import pytest

from nextagent.core.changeset import ChangeSet, Edit
from nextagent.store.db import Store


def test_dependency_mutation_during_validation_rejects_commit(tmp_path):
    (tmp_path / "api.h").write_text("int api(void);\n", encoding="utf-8")
    (tmp_path / "main.c").write_text('#include "api.h"\n', encoding="utf-8")
    with Store(tmp_path) as store:
        class Validator:
            read_set = {}

            def __call__(self, _changes):
                assert not store.db.in_transaction
                self.read_set = {"main.c": store.snapshot_text("main.c")}
                (tmp_path / "main.c").write_text("int changed;\n", encoding="utf-8")

        with pytest.raises(ValueError, match="stale_dependency: main.c"):
            store.edit(
                {"api.h": ChangeSet((Edit(0, 0, "/* edit */\n"),))},
                validator=Validator(),
            )
        assert store.head() == 0
        assert store.snapshot_text("api.h") == "int api(void);\n"


def test_unchanged_dependency_allows_commit(tmp_path):
    (tmp_path / "api.h").write_text("int api(void);\n", encoding="utf-8")
    (tmp_path / "main.c").write_text('#include "api.h"\n', encoding="utf-8")
    with Store(tmp_path) as store:
        class Validator:
            read_set = {}

            def __call__(self, _changes):
                self.read_set = {"main.c": store.snapshot_text("main.c")}

        store.edit(
            {"api.h": ChangeSet((Edit(0, 0, "/* edit */\n"),))},
            validator=Validator(),
        )
        assert store.head() == 1
