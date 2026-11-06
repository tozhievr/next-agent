import json

from nextagent.lsp.compilation import compilation_commands
from nextagent.lsp.dependencies import dependent_sources


def test_include_paths_are_scoped_to_translation_units(tmp_path):
    first = tmp_path / "first"
    second = tmp_path / "second"
    first.mkdir()
    second.mkdir()
    (first / "api.h").write_text("int first(void);\n", encoding="utf-8")
    (second / "api.h").write_text("int second(void);\n", encoding="utf-8")
    for name in ("a.c", "b.c"):
        (tmp_path / name).write_text('#include "api.h"\n', encoding="utf-8")
    (tmp_path / "compile_commands.json").write_text(json.dumps([
        {"directory": str(tmp_path), "file": "a.c",
         "arguments": ["clang", "-Ifirst", "-c", "a.c"]},
        {"directory": str(tmp_path), "file": "b.c",
         "arguments": ["clang", "-Isecond", "-c", "b.c"]},
    ]), encoding="utf-8")
    commands = compilation_commands(tmp_path)
    assert commands == {"a.c": (first,), "b.c": (second,)}
    assert dependent_sources(
        tmp_path, {"first/api.h"}, include_dirs_by_source=commands
    ) == {"a.c"}
    assert dependent_sources(
        tmp_path, {"second/api.h"}, include_dirs_by_source=commands
    ) == {"b.c"}


def test_header_chain_inherits_translation_unit_include_paths(tmp_path):
    include = tmp_path / "include"
    include.mkdir()
    (include / "api.h").write_text("int api(void);\n", encoding="utf-8")
    (include / "bridge.h").write_text('#include "api.h"\n', encoding="utf-8")
    (tmp_path / "main.c").write_text('#include "bridge.h"\n', encoding="utf-8")
    assert dependent_sources(
        tmp_path, {"include/api.h"},
        include_dirs_by_source={"main.c": (include,)},
    ) == {"main.c"}
