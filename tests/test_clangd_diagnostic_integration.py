"""Opt-in live clangd diagnostic regression test."""
import os
import shutil

import pytest

from nextagent.core.changeset import ChangeSet, Edit
from nextagent.lsp.client import ClangdClient
from nextagent.lsp.gate import DiagnosticGate
from nextagent.store.db import Store


@pytest.mark.skipif(
    os.environ.get("NEXTAGENT_TEST_CLANGD") != "1" or shutil.which("clangd") is None,
    reason="requires opt-in and clangd installed",
)
def test_real_clangd_rejects_new_parse_error(tmp_path):
    original = "int main(void) { return 0; }\n"
    (tmp_path / "main.c").write_text(original, encoding="utf-8")
    with ClangdClient(tmp_path, timeout=30.0) as client, Store(tmp_path) as store:
        gate = DiagnosticGate(tmp_path, client)
        with pytest.raises(ValueError, match="diagnostic_regression"):
            store.edit(
                {"main.c": ChangeSet((Edit(0, 0, "int x = ;\n"),))},
                if_rev=0, validator=gate,
            )
        assert store.head() == 0
        assert store.view("main.c").text == original
        assert (tmp_path / "main.c").read_text(encoding="utf-8") == original
