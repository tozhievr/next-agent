"""Conservative direct-include dependency discovery for C translation units.

This is a bounded heuristic, not a preprocessor: computed includes, macro
expansion, conditional compilation and generated headers are not resolved.
"""
import re
from collections.abc import Callable
from pathlib import Path

_INCLUDE = re.compile(r'^\s*#\s*include\s*"([^"\n]+)"', re.MULTILINE)
_SOURCE_SUFFIXES = frozenset({".c", ".cc", ".cpp", ".cxx"})
_HEADER_SUFFIXES = frozenset({".h", ".hh", ".hpp", ".hxx"})


def dependent_sources(
    root: Path,
    changed: set[str],
    *,
    max_files: int = 4096,
    snapshot_text: Callable[[str], str] | None = None,
) -> set[str]:
    """Find source files with a transitive literal-quoted include of changed headers.

    The optional snapshot_text reader supplies current Store text for each file.
    Paths are relative to root. Fail closed when the scan exceeds max_files,
    or a symlink resolves outside the workspace.
    """
    root = root.resolve()
    if max_files < 1:
        raise ValueError("max_files must be positive")
    changed_paths = {(root / name).resolve() for name in changed}
    if any(not path.is_relative_to(root) for path in changed_paths):
        raise ValueError("path escapes workspace")
    if not any(path.suffix.lower() in _HEADER_SUFFIXES for path in changed_paths):
        return set()

    dependencies: dict[Path, set[Path]] = {}
    count = 0
    for path in root.rglob("*"):
        if not path.is_file() or path.suffix.lower() not in (
            _SOURCE_SUFFIXES | _HEADER_SUFFIXES
        ):
            continue
        resolved = path.resolve()
        if not resolved.is_relative_to(root):
            raise ValueError("symlink escapes workspace")
        count += 1
        if count > max_files:
            raise ValueError("dependency_scan_limit_exceeded")
        name = resolved.relative_to(root).as_posix()
        source = (
            snapshot_text(name) if snapshot_text is not None
            else path.read_text(encoding="utf-8")
        )
        includes: set[Path] = set()
        for include in _INCLUDE.findall(source):
            target = (path.parent / include).resolve()
            if not target.is_relative_to(root):
                raise ValueError("include escapes workspace")
            includes.add(target)
        dependencies[resolved] = includes

    impacted = set(changed_paths)
    while True:
        discovered = {
            source for source, includes in dependencies.items()
            if includes & impacted
        }
        if discovered <= impacted:
            break
        impacted |= discovered
    return {
        path.relative_to(root).as_posix()
        for path in impacted
        if path.suffix.lower() in _SOURCE_SUFFIXES and path not in changed_paths
    }
