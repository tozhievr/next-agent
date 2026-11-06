import pytest

from nextagent.lsp.dependencies import dependent_sources


def test_transitive_quoted_include_dependency(tmp_path):
    (tmp_path / "api.h").write_text("int api(void);\n", encoding="utf-8")
    (tmp_path / "bridge.h").write_text('#include "api.h"\n', encoding="utf-8")
    (tmp_path / "main.c").write_text('#include "bridge.h"\n', encoding="utf-8")
    (tmp_path / "other.c").write_text("int other(void);\n", encoding="utf-8")
    assert dependent_sources(tmp_path, {"api.h"}) == {"main.c"}


def test_source_edit_has_no_header_dependents(tmp_path):
    (tmp_path / "main.c").write_text("int main(void);\n", encoding="utf-8")
    assert dependent_sources(tmp_path, {"main.c"}) == set()


def test_dependency_scan_limit_is_fail_closed(tmp_path):
    (tmp_path / "api.h").write_text("", encoding="utf-8")
    (tmp_path / "a.c").write_text('#include "api.h"\n', encoding="utf-8")
    with pytest.raises(ValueError, match="dependency_scan_limit_exceeded"):
        dependent_sources(tmp_path, {"api.h"}, max_files=1)


def test_reject_workspace_escape(tmp_path):
    with pytest.raises(ValueError, match="path escapes workspace"):
        dependent_sources(tmp_path, {"../api.h"})
