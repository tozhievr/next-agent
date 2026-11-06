"""Line-delimited JSON-RPC 2.0 transport for NextAgent.

One JSON object per line; this is NOT LSP Content-Length framing or MCP.
"""
import json
import sys
from pathlib import Path
from typing import TextIO

from nextagent import __version__
from nextagent.core.changeset import ChangeSet, Edit
from nextagent.store.db import Store


def _edits(ops: list[dict]) -> dict[str, ChangeSet]:
    batches: dict[str, list[Edit]] = {}
    for op in ops:
        batches.setdefault(op["path"], []).append(Edit(op["start"], op["end"], op["text"]))
    return {
        path: ChangeSet(tuple(sorted(edits, key=lambda e: (e.start, e.end))))
        for path, edits in batches.items()
    }


def dispatch(store: Store, request: object) -> dict | None:
    """Handle one JSON-RPC request; notifications intentionally return no reply."""
    if not isinstance(request, dict):
        return _error(None, -32600, "Invalid Request")
    has_id = "id" in request
    request_id = request.get("id")
    if (
        request.get("jsonrpc") != "2.0"
        or not isinstance(request.get("method"), str)
        or (has_id and (isinstance(request_id, bool) or not (
            request_id is None or isinstance(request_id, (str, int))
        )))
    ):
        return _error(request_id if has_id else None, -32600, "Invalid Request")
    method = request["method"]
    params = request.get("params", {})
    if not isinstance(params, dict):
        return None if not has_id else _error(request_id, -32602, "Invalid params")
    try:
        if method == "open":
            result = {"name": "NextAgent", "version": __version__, "rev": store.head()}
        elif method == "view":
            snap = store.view(params["target"])
            result = {"rev": snap.rev, "path": params["target"], "text": snap.text}
        elif method in ("outline", "select"):
            from nextagent.semantic.c23 import outline, select

            snapshot = store.view(params["target"])
            if method == "outline":
                result = {"symbols": [symbol.to_dict() for symbol in outline(
                    snapshot, nested=params.get("nested", False)
                )]}
            else:
                result = select(
                    snapshot, params["name"], params.get("kind"), params.get("scope")
                ).to_dict()
        elif method == "replace_symbol":
            from nextagent.semantic.c23 import replace_symbol

            target = params["target"]
            snapshot = store.view(target)
            expected = params["if_rev"]
            # A document snapshot revision and the workspace revision are distinct:
            # require the current workspace head and compare it to the requested head.
            if expected != store.head():
                raise ValueError(f"stale_rev: expected {expected}, current {store.head()}")
            changes = replace_symbol(
                snapshot, params["name"], params["replacement"],
                params.get("kind"), params.get("scope"),
            )
            result = store.edit(
                {target: changes}, if_rev=expected,
                label=params.get("label", "replace_symbol"), session_id="jsonrpc",
            )
        elif method == "edit":
            result = store.edit(
                _edits(params["ops"]),
                if_rev=params.get("if_rev"),
                label=params.get("label", "edit"),
                session_id="jsonrpc",
            )
        elif method == "history":
            operation = params.get("op", "log")
            if operation == "log":
                result = store.history(params.get("n", 20))
            elif operation == "undo":
                result = store.undo()
            else:
                raise ValueError("unsupported history operation")
        elif method == "sync":
            if params.get("op", "flush") != "flush":
                raise ValueError("unsupported sync operation")
            result = {"ok": True, "written": store.flush(params.get("paths"))}
        else:
            return None if not has_id else _error(request_id, -32601, "Method not found")
    except (KeyError, TypeError, ValueError) as exc:
        return None if not has_id else _error(request_id, -32602, str(exc))
    if not has_id:
        return None
    return {"jsonrpc": "2.0", "id": request_id, "result": result}


def _error(request_id: str | int | None, code: int, message: str) -> dict:
    return {"jsonrpc": "2.0", "id": request_id, "error": {"code": code, "message": message}}


def serve(root: Path, source: TextIO | None = None, sink: TextIO | None = None) -> None:
    """Serve JSON objects delimited by newlines until EOF."""
    incoming = source if source is not None else sys.stdin
    outgoing = sink if sink is not None else sys.stdout
    with Store(root) as store:
        for line in incoming:
            if not line.strip():
                continue
            try:
                request = json.loads(line)
            except json.JSONDecodeError:
                reply = _error(None, -32700, "Parse error")
            else:
                reply = dispatch(store, request)
            if reply is not None:
                outgoing.write(json.dumps(reply, ensure_ascii=False) + "\n")
                outgoing.flush()
