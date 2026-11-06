"""Opt-in live clangd smoke test: NEXTAGENT_TEST_CLANGD=1 pytest -q."""
import os
import shutil

import pytest

from nextagent.lsp.client import ClangdClient


@pytest.mark.skipif(
    os.environ.get("NEXTAGENT_TEST_CLANGD") != "1" or shutil.which("clangd") is None,
    reason="requires opt-in and clangd installed",
)
def test_live_clangd_initialize_and_format(tmp_path):
    path = tmp_path / "hello.c"
    text = "int main(void){return 0;}\n"
    path.write_text(text, encoding="utf-8")
    with ClangdClient(tmp_path, timeout=20.0) as client:
        assert client.encoding in {"utf-8", "utf-16", "utf-32"}
        uri = client.open_document(path, text)
        edits = client.formatting(uri)
        assert isinstance(edits, list)
        client.notify("textDocument/didClose", {"textDocument": {"uri": uri}})
