import pytest

from nextagent.core.changeset import ChangeSet, Edit
from nextagent.core.snapshot import Snapshot


def test_unicode_codepoint_offsets():
    snap = Snapshot.of(1, 0, "а😀b\n你好\n")
    assert snap.position(2) == (1, 2)
    assert snap.offset(2, 1) == 5
    assert snap.position(len(snap.text)) == (3, 0)


def test_inverse_and_position_mapping():
    snap = Snapshot.of(1, 0, "one two three")
    edits = ChangeSet((Edit(0, 3, "ONE"), Edit(8, 13, "THREE!")))
    after = edits.apply(snap)
    assert after.text == "ONE two THREE!"
    assert edits.invert(snap).apply(after).text == snap.text
    assert edits.map_pos(8, assoc=-1) == 8
    assert edits.map_pos(13) == 14


def test_reject_overlap():
    with pytest.raises(ValueError):
        ChangeSet((Edit(0, 3, "x"), Edit(2, 4, "y")))


def test_reject_out_of_bounds():
    with pytest.raises(ValueError):
        ChangeSet((Edit(0, 99, ""),)).apply(Snapshot.of(1, 0, "short"))
