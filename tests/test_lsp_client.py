from pathlib import Path
from unittest.mock import MagicMock, patch

import pytest

from nextagent.lsp.client import ClangdClient


def test_missing_clangd_is_explicit(tmp_path):
    client = ClangdClient(tmp_path, executable="not-a-real-clangd-binary")
    with pytest.raises(FileNotFoundError, match="clangd executable"):
        client.start()


def test_request_correlates_response(tmp_path):
    client = ClangdClient(tmp_path)
    client.process = MagicMock()
    client.process.stdin = MagicMock()

    def fake_write(_stream, message):
        with client._lock:
            waiter = client._pending[message["id"]]
        waiter.put({"jsonrpc": "2.0", "id": message["id"], "result": {"ok": True}})

    with patch("nextagent.lsp.client.write_message", side_effect=fake_write):
        assert client.request("test", {}) == {"ok": True}
    assert not client._pending


def test_open_rejects_external_path(tmp_path):
    client = ClangdClient(tmp_path)
    with pytest.raises(ValueError, match="escapes workspace"):
        client.open_document(tmp_path.parent / "outside.c", "")


def test_diagnostics_are_snapshots(tmp_path):
    client = ClangdClient(tmp_path)
    assert client.diagnostics(Path(tmp_path / "x.c").as_uri()) is None
