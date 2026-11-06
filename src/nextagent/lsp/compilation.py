"""Bounded compilation database reader for literal include search directories.

Only -I, -iquote and /I are interpreted. This is not a compiler driver.
"""
import json
import shlex
from pathlib import Path


def compilation_include_dirs(root: Path, *, max_entries: int = 4096) -> tuple[Path, ...]:
    """Read compile_commands.json from root or root/build.

    Reject malformed data and workspace-escaping include directories. No shell
    execution or response-file expansion occurs.
    """
    root = root.resolve()
    databases = [root / "compile_commands.json", root / "build" / "compile_commands.json"]
    database = next((path for path in databases if path.is_file()), None)
    if database is None:
        return ()
    entries = json.loads(database.read_text(encoding="utf-8"))
    if not isinstance(entries, list) or len(entries) > max_entries:
        raise ValueError("invalid_compilation_database")
    paths: set[Path] = set()
    for entry in entries:
        if not isinstance(entry, dict) or not isinstance(entry.get("directory"), str):
            raise ValueError("invalid_compilation_database_entry")
        cwd = (database.parent / entry["directory"]).resolve() if not Path(
            entry["directory"]
        ).is_absolute() else Path(entry["directory"]).resolve()
        if not cwd.is_relative_to(root):
            raise ValueError("compilation_directory_escapes_workspace")
        if "arguments" in entry:
            args = entry["arguments"]
            if not isinstance(args, list) or any(not isinstance(arg, str) for arg in args):
                raise ValueError("invalid_compilation_arguments")
        elif isinstance(entry.get("command"), str):
            args = shlex.split(entry["command"])
        else:
            raise ValueError("invalid_compilation_command")
        index = 0
        while index < len(args):
            arg = args[index]
            value = None
            if arg in ("-I", "-iquote", "/I"):
                index += 1
                if index >= len(args):
                    raise ValueError("missing_include_directory")
                value = args[index]
            elif arg.startswith("-iquote") and len(arg) > 7:
                value = arg[7:]
            elif arg.startswith("-I") and len(arg) > 2:
                value = arg[2:]
            elif arg.startswith("/I") and len(arg) > 2:
                value = arg[2:]
            if value is not None:
                target = (cwd / value).resolve()
                if not target.is_relative_to(root):
                    raise ValueError("include_directory_escapes_workspace")
                paths.add(target)
            index += 1
    return tuple(sorted(paths))
