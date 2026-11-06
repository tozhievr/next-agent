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
    outline_cmd = commands.add_parser("outline", help="C23 top-level symbols")
    outline_cmd.add_argument("path")
    select_cmd = commands.add_parser("select", help="C23 symbol range")
    select_cmd.add_argument("path")
    select_cmd.add_argument("name")
    select_cmd.add_argument("--kind")
    commands.add_parser("undo")
    history = commands.add_parser("history")
    history.add_argument("--limit", type=int, default=20)
    sync = commands.add_parser("sync")
    sync.add_argument("paths", nargs="*")
    serve_cmd = commands.add_parser("serve", help="JSON-RPC 2.0 over line-delimited stdio")
    serve_cmd.add_argument("--clangd", nargs="?", const="clangd", default=None,
                           help="enable strict pre-commit diagnostics with clangd")
    args = parser.parse_args()
    if args.cmd == "serve":
        from nextagent.proto.rpc import serve

        serve(args.root, clangd=args.clangd)
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
        elif args.cmd in ("outline", "select"):
            from nextagent.semantic.c23 import outline, select

            snapshot = store.view(args.path)
            if args.cmd == "outline":
                result = {"symbols": [symbol.to_dict() for symbol in outline(snapshot)]}
            else:
                result = select(snapshot, args.name, args.kind).to_dict()
        elif args.cmd == "undo":
            result = store.undo()
        elif args.cmd == "history":
            result = store.history(args.limit)
        else:
            result = {"ok": True, "written": store.flush(args.paths or None)}
        print(json.dumps(result, ensure_ascii=False))


if __name__ == "__main__":
    main()
