"""Immutable Unicode codepoint-based document snapshots."""

from bisect import bisect_right
from dataclasses import dataclass
from hashlib import blake2b


@dataclass(frozen=True, slots=True)
class Snapshot:
    doc_id: int
    rev: int
    text: str
    line_starts: tuple[int, ...]
    hash: bytes

    @classmethod
    def of(cls, doc_id: int, rev: int, text: str) -> "Snapshot":
        starts = (0, *(i + 1 for i, c in enumerate(text) if c == "\n"))
        digest = blake2b(text.encode("utf-8"), digest_size=16).digest()
        return cls(doc_id, rev, text, starts, digest)

    def offset(self, line: int, column: int = 0) -> int:
        """Convert a 1-based line and a 0-based codepoint column to an offset."""
        if not 1 <= line <= len(self.line_starts) or column < 0:
            raise ValueError("invalid line or column")
        start = self.line_starts[line - 1]
        end = self.line_starts[line] if line < len(self.line_starts) else len(self.text)
        if start + column > end:
            raise ValueError("column exceeds line")
        return start + column

    def position(self, offset: int) -> tuple[int, int]:
        """Return a 1-based line and 0-based codepoint column."""
        if not 0 <= offset <= len(self.text):
            raise ValueError("offset outside document")
        line = bisect_right(self.line_starts, offset)
        return line, offset - self.line_starts[line - 1]
