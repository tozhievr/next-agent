import pytest

from nextagent.core.changeset import ChangeSet, Edit
from nextagent.lsp.gate import DiagnosticGate
from nextagent.store.db import Store


def test_compilation_database_change_rejects_commit(tmp_path):
    (tmp_path / "api.h").write_text("int api;\n", encoding="utf-8")
    database = tmp_path / "compile_commands.json"
    database.write_text("[]", encoding="utf-8")
    with Store(tmp_path) as store:
        class Validator:
            build_state = None

            def _build_state(self):
                return DiagnosticGate._build_state(self)

            root = tmp_path

            def __call__(self, _changes):
                self.build_state = self._build_state()
                database.write_text('[{"file":"new.c"}]', encoding="utf-8")

        with pytest.raises(ValueError, match="stale_build_state"):
            store.edit(
                {"api.h": ChangeSet((Edit(0, 0, "/* edit */\n"),))},
                validator=Validator(),
            )
        assert store.head() == 0


def test_new_translation_unit_during_validation_rejects_commit(tmp_path):
    (tmp_path / "api.h").write_text("int api;\n", encoding="utf-8")
    with Store(tmp_path) as store:
        class Validator:
            build_state = None
            root = tmp_path

            def _build_state(self):
                return DiagnosticGate._build_state(self)

            def __call__(self, _changes):
                self.build_state = self._build_state()
                (tmp_path / "new.c").write_text("int new;\n", encoding="utf-8")

        with pytest.raises(ValueError, match="stale_build_state"):
            store.edit(
                {"api.h": ChangeSet((Edit(0, 0, "/* edit */\n"),))},
                validator=Validator(),
            )
        assert store.head() == 0


def test_stable_build_state_allows_commit(tmp_path):
    (tmp_path / "api.h").write_text("int api;\n", encoding="utf-8")
    with Store(tmp_path) as store:
        class Validator:
            build_state = None
            root = tmp_path

            def _build_state(self):
                return DiagnosticGate._build_state(self)

            def __call__(self, _changes):
                self.build_state = self._build_state()

        store.edit(
            {"api.h": ChangeSet((Edit(0, 0, "/* edit */\n"),))},
            validator=Validator(),
        )
        assert store.head() == 1
