"""Bounded literal quoted-include dependency discovery.

Source-specific include paths are inherited through nested header traversal.
Not a preprocessor: macros, conditions and generated files are unsupported.
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
    include_dirs: tuple[Path, ...] = (),
    include_dirs_by_source: dict[str, tuple[Path, ...]] | None = None,
) -> set[str]:
    """Return source files whose resolved quoted-include closure touches changed headers."""
    root = root.resolve()
    if max_files < 1:
        raise ValueError("max_files must be positive")
    changed_paths = {(root / name).resolve() for name in changed}
    if any(not path.is_relative_to(root) for path in changed_paths):
        raise ValueError("path escapes workspace")
    if not any(path.suffix.lower() in _HEADER_SUFFIXES for path in changed_paths):
        return set()
    sources: list[Path] = []
    for path in root.rglob("*"):
        if not path.is_file() or path.suffix.lower() not in (
            _SOURCE_SUFFIXES | _HEADER_SUFFIXES
        ):
            continue
        resolved = path.resolve()
        if not resolved.is_relative_to(root):
            raise ValueError("symlink escapes workspace")
        sources.append(resolved)
        if len(sources) > max_files:
            raise ValueError("dependency_scan_limit_exceeded")
    discovered: set[str] = set()
    for source in sources:
        if source.suffix.lower() not in _SOURCE_SUFFIXES or source in changed_paths:
            continue
        name = source.relative_to(root).as_posix()
        directories = (
            include_dirs_by_source.get(name, include_dirs)
            if include_dirs_by_source is not None else include_dirs
        )
        if any(not directory.resolve().is_relative_to(root) for directory in directories):
            raise ValueError("include_directory_escapes_workspace")
        pending = [source]
        visited: set[Path] = set()
        while pending:
            current = pending.pop()
            if current in visited:
                continue
            visited.add(current)
            if len(visited) > max_files:
                raise ValueError("dependency_scan_limit_exceeded")
            if current in changed_paths:
                discovered.add(name)
                break
            if not current.is_file():
                continue
            relative = current.relative_to(root).as_posix()
            text = (
                snapshot_text(relative) if snapshot_text is not None
                else current.read_text(encoding="utf-8")
            )
            for include in _INCLUDE.findall(text):
                for candidate in (
                    current.parent / include,
                    *(directory / include for directory in directories),
                ):
                    target = candidate.resolve()
                    if not target.is_relative_to(root):
                        raise ValueError("include escapes workspace")
                    if target.is_file():
                        pending.append(target)
                        break
    return discovered
