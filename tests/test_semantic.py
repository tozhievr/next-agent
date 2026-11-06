import pytest

from nextagent.core.snapshot import Snapshot
from nextagent.semantic.c23 import outline, replace_symbol, select


def test_unicode_byte_to_codepoint_mapping():
    text = "/* 😀 */\nint sum(int a, int b) { return a + b; }\n"
    snapshot = Snapshot.of(1, 0, text)
    sym = select(snapshot, "sum", "function")
    assert sym.start == text.index("int sum")
    assert sym.line == 2
    assert sym.column == 0
    assert text[sym.start:sym.end].startswith("int sum")
    assert replace_symbol(snapshot, "sum", "int sum(void) { return 1; }").apply(
        snapshot
    ).text.startswith("/* 😀 */\nint sum(void)")


def test_outline_functions_and_typedef():
    snapshot = Snapshot.of(
        1, 0, "typedef unsigned long size_t;\n"
        "int compute(void) { return 2; }\n"
        "int main(void) { return compute(); }\n",
    )
    symbols = outline(snapshot)
    assert ("compute", "function") in [(s.name, s.kind) for s in symbols]
    assert ("main", "function") in [(s.name, s.kind) for s in symbols]


def test_missing_and_ambiguous_symbols():
    snap = Snapshot.of(1, 0, "int a(void) {return 1;}\nint a(void) {return 2;}\n")
    with pytest.raises(ValueError, match="ambiguous_symbol"):
        select(snap, "a")
    with pytest.raises(ValueError, match="symbol_not_found"):
        select(snap, "missing")


def test_symbol_replacement_uses_existing_revision():
    snap = Snapshot.of(1, 7, "int main(void) { return 0; }\n")
    changes = replace_symbol(snap, "main", "int main(void) { return 42; }")
    assert changes.apply(snap).text == "int main(void) { return 42; }\n"
