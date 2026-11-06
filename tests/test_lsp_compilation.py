import json

import pytest

from nextagent.lsp.compilation import compilation_include_dirs
from nextagent.lsp.dependencies import dependent_sources


def test_compile_commands_quoted_include_directory(tmp_path):
    include = tmp_path / "include"
    include.mkdir()
    (include / "api.h").write_text("int api(void);\n", encoding="utf-8")
    (tmp_path / "main.c").write_text('#include "api.h"\n', encoding="utf-8")
    (tmp_path / "compile_commands.json").write_text(json.dumps([{
        "directory": str(tmp_path),
        "file": "main.c",
        "arguments": ["clang", "-I", "include", "-c", "main.c"],
    }]), encoding="utf-8")
    assert compilation_include_dirs(tmp_path) == (include,)
    assert dependent_sources(tmp_path, {"include/api.h"}) == set()
    assert dependent_sources(
        tmp_path, {"include/api.h"}, include_dirs=compilation_include_dirs(tmp_path)
    ) == {"main.c"}


def test_compile_commands_rejects_escape(tmp_path):
    (tmp_path / "compile_commands.json").write_text(json.dumps([{
        "directory": str(tmp_path),
        "file": "main.c",
        "arguments": ["clang", "-I", "../outside", "main.c"],
    }]), encoding="utf-8")
    with pytest.raises(ValueError, match="include_directory_escapes_workspace"):
        compilation_include_dirs(tmp_path)


def test_compile_commands_accepts_command_string(tmp_path):
    (tmp_path / "include").mkdir()
    (tmp_path / "compile_commands.json").write_text(json.dumps([{
        "directory": str(tmp_path),
        "file": "main.c",
        "command": "clang -Iinclude -c main.c",
    }]), encoding="utf-8")
    assert compilation_include_dirs(tmp_path) == (tmp_path / "include",)


def test_no_compilation_database(tmp_path):
    assert compilation_include_dirs(tmp_path) == ()
