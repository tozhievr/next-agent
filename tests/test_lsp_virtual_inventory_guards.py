import pytest

from nextagent.lsp.dependencies import dependent_sources


def test_virtual_inventory_requires_snapshot_reader(tmp_path):
    (tmp_path / "api.h").write_text("int api;\n", encoding="utf-8")
    with pytest.raises(ValueError, match="snapshot_paths_requires_snapshot_text"):
        dependent_sources(
            tmp_path,
            {"api.h"},
            snapshot_paths=("new.c",),
        )


def test_large_virtual_inventory_honors_file_limit(tmp_path):
    (tmp_path / "api.h").write_text("int api;\n", encoding="utf-8")
    with pytest.raises(ValueError, match="dependency_scan_limit_exceeded"):
        dependent_sources(
            tmp_path,
            {"api.h"},
            max_files=2,
            snapshot_text=lambda _name: "",
            snapshot_paths=("a.c", "b.c"),
        )
