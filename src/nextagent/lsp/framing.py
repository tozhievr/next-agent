"""Binary LSP Content-Length framing (independent of NextAgent JSON-RPC stdio)."""
import json
from typing import BinaryIO

_MAX_MESSAGE = 16 * 1024 * 1024
_MAX_HEADER = 8192


def read_message(stream: BinaryIO) -> object | None:
    """Read one Content-Length framed JSON message, or None at clean EOF."""
    total = 0
    length = None
    while True:
        line = stream.readline(_MAX_HEADER + 1)
        if not line:
            if total == 0:
                return None
            raise EOFError("truncated LSP header")
        total += len(line)
        if total > _MAX_HEADER:
            raise ValueError("LSP header too large")
        if line == b"\r\n":
            break
        if not line.endswith(b"\r\n"):
            raise ValueError("malformed LSP header")
        key, sep, value = line[:-2].partition(b":")
        if not sep:
            raise ValueError("malformed LSP header")
        if key.lower() == b"content-length":
            if length is not None or not value.strip().isdigit():
                raise ValueError("invalid LSP Content-Length")
            length = int(value.strip())
    if length is None or length > _MAX_MESSAGE:
        raise ValueError("missing or oversized LSP Content-Length")
    body = stream.read(length)
    if len(body) != length:
        raise EOFError("truncated LSP body")
    return json.loads(body.decode("utf-8"))


def write_message(stream: BinaryIO, message: object) -> None:
    body = json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    if len(body) > _MAX_MESSAGE:
        raise ValueError("LSP message too large")
    stream.write(b"Content-Length: " + str(len(body)).encode("ascii") + b"\r\n\r\n" + body)
    stream.flush()
