import io

import pytest

from nextagent.lsp.codec import offset_to_position, position_to_offset
from nextagent.lsp.framing import read_message, write_message


@pytest.mark.parametrize("encoding,character", [
    ("utf-8", 5), ("utf-16", 3), ("utf-32", 2),
])
def test_unicode_positions(encoding, character):
    text = "a😀z\n你好"
    assert offset_to_position(text, 2, encoding).character == character
    assert position_to_offset(text, 0, character, encoding) == 2
    assert position_to_offset(text, 1, 6 if encoding == "utf-8" else 2, encoding) == len(text)


def test_invalid_mid_surrogate_and_utf8_sequence():
    with pytest.raises(ValueError, match="splits"):
        position_to_offset("😀", 0, 1, "utf-16")
    with pytest.raises(ValueError, match="splits"):
        position_to_offset("😀", 0, 2, "utf-8")


def test_lsp_frame_roundtrip_non_ascii():
    stream = io.BytesIO()
    message = {"jsonrpc": "2.0", "method": "textDocument/publishDiagnostics",
               "params": {"message": "你好😀"}}
    write_message(stream, message)
    stream.seek(0)
    assert read_message(stream) == message
    assert read_message(stream) is None


def test_reject_bad_frames():
    for data in (
        b"Content-Length: 3\r\n\r\n{}",
        b"Content-Length: 999999999\r\n\r\n",
        b"Content-Length: x\r\n\r\n",
        b"Content-Type: application/json\r\n\r\n{}",
    ):
        with pytest.raises((ValueError, EOFError)):
            read_message(io.BytesIO(data))
