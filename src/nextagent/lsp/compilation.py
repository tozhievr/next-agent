"""Conservative compilation database reader for workspace-local include directories.

No compiler execution, response-file expansion, macro evaluation or system
header discovery. Include paths are scoped to individual translation units.
"""
import json
import shlex
from pathlib import Path


def compilation_commands(
    root: Path, *, max_entries: int = 4096
) -> dict[str, tuple[Path, ...]]:
    """Map workspace-relative translation units to their include search paths."""
    root = root.resolve()
    databases = [root / "compile_commands.json", root / "build" / "compile_commands.json"]
    database = next((path for path in databases if path.is_file()), None)
    if database is None:
        return {}
    entries = json.loads(database.read_text(encoding="utf-8"))
    if not isinstance(entries, list) or len(entries) > max_entries:
        raise ValueError("invalid_compilation_database")
    result: dict[str, tuple[Path, ...]] = {}
    for entry in entries:
        if not isinstance(entry, dict) or not isinstance(entry.get("directory"), str):
            raise ValueError("invalid_compilation_database_entry")
        directory = Path(entry["directory"])
        cwd = (directory if directory.is_absolute() else database.parent / directory).resolve()
        if not cwd.is_relative_to(root):
            raise ValueError("compilation_directory_escapes_workspace")
        filename = entry.get("file")
        if not isinstance(filename, str):
            raise ValueError("invalid_compilation_file")
        source = (cwd / filename).resolve()
        if not source.is_relative_to(root):
            raise ValueError("compilation_file_escapes_workspace")
        if "arguments" in entry:
            args = entry["arguments"]
            if not isinstance(args, list) or any(not isinstance(arg, str) for arg in args):
                raise ValueError("invalid_compilation_arguments")
        elif isinstance(entry.get("command"), str):
            args = shlex.split(entry["command"])
        else:
            raise ValueError("invalid_compilation_command")
        paths: list[Path] = []
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
                if target not in paths:
                    paths.append(target)
            index += 1
        key = source.relative_to(root).as_posix()
        # Multiple configurations for one source are conservatively combined.
        result[key] = tuple(dict.fromkeys((*result.get(key, ()), *paths)))
    return result


def compilation_include_dirs(root: Path, *, max_entries: int = 4096) -> tuple[Path, ...]:
    """Compatibility helper: union of include paths (not TU-scoped)."""
    return tuple(sorted({
        path for paths in compilation_commands(root, max_entries=max_entries).values()
        for path in paths
    }))
