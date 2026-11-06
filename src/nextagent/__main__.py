"""Minimal M0 command-line editor."""

import argparse
import json
import sys
from pathlib import Path

from nextagent import __version__
from nextagent.core.changeset import ChangeSet, Edit
from nextagent.store.db import Store


def main() -> None:
    parser = argparse.ArgumentParser(prog="nextagent")
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--version", action="version", version=f"NextAgent {__version__}")
    commands = parser.add_subparsers(dest="cmd", required=True)
    view = commands.add_parser("view")
    view.add_argument("path")
    edit = commands.add_parser("edit")
    edit.add_argument("--request", type=Path, help="JSON request file, otherwise stdin")
    commands.add_parser("undo")
    history = commands.add_parser("history")
    history.add_argument("--limit", type=int, default=20)
    sync = commands.add_parser("sync")
    sync.add_argument("paths", nargs="*")
    commands.add_parser("serve", help="JSON-RPC 2.0 over line-delimited stdio")
    args = parser.parse_args()
    if args.cmd == "serve":
        from nextagent.proto.rpc import serve

        serve(args.root)
        return
    with Store(args.root) as store:
        if args.cmd == "view":
            snapshot = store.view(args.path)
            result = {"rev": snapshot.rev, "text": snapshot.text}
        elif args.cmd == "edit":
            request = json.loads(
                args.request.read_text(encoding="utf-8") if args.request else sys.stdin.read()
            )
            batches: dict[str, list[Edit]] = {}
            for operation in request["ops"]:
                batches.setdefault(operation["path"], []).append(
                    Edit(operation["start"], operation["end"], operation["text"])
                )
            result = store.edit(
                {
                    path: ChangeSet(tuple(sorted(edits, key=lambda e: (e.start, e.end))))
                    for path, edits in batches.items()
                },
                if_rev=request.get("if_rev"),
                label=request.get("label", "edit"),
            )
        elif args.cmd == "undo":
            result = store.undo()
        elif args.cmd == "history":
            result = store.history(args.limit)
        else:
            result = {"ok": True, "written": store.flush(args.paths or None)}
        print(json.dumps(result, ensure_ascii=False))


if __name__ == "__main__":
    main()
