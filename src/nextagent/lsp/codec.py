"""Strict LSP position conversions for UTF-8, UTF-16 and UTF-32."""
from dataclasses import dataclass

_ENCODINGS = frozenset(("utf-8", "utf-16", "utf-32"))


@dataclass(frozen=True, slots=True)
class Position:
    line: int
    character: int


def _lines(text: str) -> list[str]:
    return text.split("\n")


def _units(text: str, encoding: str) -> int:
    if encoding == "utf-32":
        return len(text)
    if encoding == "utf-16":
        return len(text.encode("utf-16-le")) // 2
    return len(text.encode("utf-8"))


def offset_to_position(text: str, offset: int, encoding: str = "utf-16") -> Position:
    """Convert Python codepoint offset into LSP line/character units."""
    if encoding not in _ENCODINGS:
        raise ValueError(f"unsupported position encoding: {encoding}")
    if not 0 <= offset <= len(text):
        raise ValueError("offset out of bounds")
    prefix = text[:offset]
    line = prefix.count("\n")
    return Position(line, _units(prefix.rsplit("\n", 1)[-1], encoding))


def position_to_offset(
    text: str, line: int, character: int, encoding: str = "utf-16"
) -> int:
    """Reject positions splitting surrogate pairs or UTF-8 sequences."""
    if encoding not in _ENCODINGS:
        raise ValueError(f"unsupported position encoding: {encoding}")
    lines = _lines(text)
    if line < 0 or line >= len(lines) or character < 0:
        raise ValueError("position out of bounds")
    segment = lines[line]
    consumed = 0
    for index, char in enumerate(segment):
        if consumed == character:
            return sum(len(part) + 1 for part in lines[:line]) + index
        consumed += _units(char, encoding)
        if consumed > character:
            raise ValueError("position splits Unicode scalar")
    if consumed == character:
        return sum(len(part) + 1 for part in lines[:line]) + len(segment)
    raise ValueError("position out of bounds")
