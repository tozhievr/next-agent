"""Atomic, non-overlapping, original-revision offset edits."""

from dataclasses import dataclass

from nextagent.core.snapshot import Snapshot


@dataclass(frozen=True, slots=True)
class Edit:
    start: int
    end: int
    text: str

    def __post_init__(self) -> None:
        if self.start < 0 or self.end < self.start:
            raise ValueError("invalid edit range")


@dataclass(frozen=True, slots=True)
class ChangeSet:
    edits: tuple[Edit, ...]

    def __post_init__(self) -> None:
        previous_end = -1
        for edit in self.edits:
            if edit.start < previous_end or (
                edit.start == previous_end and edit.start == edit.end
            ):
                raise ValueError("edits must be sorted and non-overlapping")
            previous_end = edit.end

    def apply(self, snap: Snapshot) -> Snapshot:
        parts: list[str] = []
        cursor = 0
        for edit in self.edits:
            if edit.end > len(snap.text):
                raise ValueError("edit beyond document end")
            parts.extend((snap.text[cursor:edit.start], edit.text))
            cursor = edit.end
        parts.append(snap.text[cursor:])
        return Snapshot.of(snap.doc_id, snap.rev + 1, "".join(parts))

    def invert(self, snap: Snapshot) -> "ChangeSet":
        inverse: list[Edit] = []
        displacement = 0
        for edit in self.edits:
            if edit.end > len(snap.text):
                raise ValueError("edit beyond document end")
            start = edit.start + displacement
            inverse.append(Edit(start, start + len(edit.text), snap.text[edit.start:edit.end]))
            displacement += len(edit.text) - (edit.end - edit.start)
        return ChangeSet(tuple(inverse))

    def map_pos(self, offset: int, assoc: int = 1) -> int:
        if offset < 0 or assoc not in (-1, 1):
            raise ValueError("invalid position or association")
        displacement = 0
        for edit in self.edits:
            if offset < edit.start or (offset == edit.start and assoc < 0):
                break
            if offset <= edit.end:
                return edit.start + displacement + (len(edit.text) if assoc > 0 else 0)
            displacement += len(edit.text) - (edit.end - edit.start)
        return offset + displacement

    def stats(self, snap: Snapshot) -> tuple[int, int]:
        added = removed = 0
        for edit in self.edits:
            if edit.end > len(snap.text):
                raise ValueError("edit beyond document end")
            added += edit.text.count("\n")
            removed += snap.text[edit.start:edit.end].count("\n")
        return added, removed
