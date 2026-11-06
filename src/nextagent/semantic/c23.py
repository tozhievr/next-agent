"""C23 syntax-based navigation with byte/codepoint offset conversion.

Tree-sitter recognizes syntactic declarations, not resolved symbol identities.
"""
from dataclasses import asdict, dataclass

import tree_sitter_c
from tree_sitter import Language, Parser

from nextagent.core.changeset import ChangeSet, Edit
from nextagent.core.snapshot import Snapshot


@dataclass(frozen=True, slots=True)
class Symbol:
    name: str
    kind: str
    start: int
    end: int
    line: int
    column: int

    def to_dict(self) -> dict:
        return asdict(self)


def _parse(text: str):
    parser = Parser(Language(tree_sitter_c.language()))
    return parser.parse(text.encode("utf-8"))


def _codepoint(text: str, byte_offset: int) -> int:
    return len(text.encode("utf-8")[:byte_offset].decode("utf-8"))


def _name(node, source: bytes) -> str | None:
    """Unwrap pointer/array/function declarators to their identifier."""
    if node is None:
        return None
    if node.type in ("identifier", "type_identifier", "field_identifier"):
        return source[node.start_byte:node.end_byte].decode("utf-8")
    for field in ("declarator", "name"):
        child = node.child_by_field_name(field)
        if child is not None:
            result = _name(child, source)
            if result:
                return result
    for child in node.named_children:
        result = _name(child, source)
        if result:
            return result
    return None


def outline(snapshot: Snapshot) -> list[Symbol]:
    """Top-level function definitions, declarations, and named type definitions."""
    source = snapshot.text.encode("utf-8")
    root = _parse(snapshot.text).root_node
    result = []
    for node in root.named_children:
        if node.type == "function_definition":
            kind = "function"
            name = _name(node.child_by_field_name("declarator"), source)
        elif node.type == "type_definition":
            kind = "typedef"
            name = _name(node.child_by_field_name("declarator"), source)
        elif node.type == "declaration":
            kind = "declaration"
            declarators = [c for c in node.named_children if c.type in (
                "init_declarator", "identifier", "pointer_declarator",
                "array_declarator", "function_declarator"
            )]
            name = next((n for c in declarators if (n := _name(c, source))), None)
        else:
            continue
        if name is None:
            continue
        start = _codepoint(snapshot.text, node.start_byte)
        end = _codepoint(snapshot.text, node.end_byte)
        line, column = snapshot.position(start)
        result.append(Symbol(name, kind, start, end, line, column))
    return result


def select(snapshot: Snapshot, name: str, kind: str | None = None) -> Symbol:
    matches = [s for s in outline(snapshot) if s.name == name and (
        kind is None or s.kind == kind
    )]
    if not matches:
        raise ValueError(f"symbol_not_found: {name}")
    if len(matches) != 1:
        raise ValueError(f"ambiguous_symbol: {name} ({len(matches)} matches)")
    return matches[0]


def replace_symbol(
    snapshot: Snapshot, name: str, replacement: str, kind: str | None = None
) -> ChangeSet:
    """Replace a complete top-level syntactic declaration, not its references."""
    symbol = select(snapshot, name, kind)
    return ChangeSet((Edit(symbol.start, symbol.end, replacement),))
