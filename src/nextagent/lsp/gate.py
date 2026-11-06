"""Pre-commit clangd diagnostic gate with bounded dependent-unit validation.

Changed documents and literal-include dependent translation units are opened
as a single overlay. This is not complete preprocessor dependency resolution.
"""
from collections.abc import Callable
from pathlib import Path

from nextagent.core.snapshot import Snapshot
from nextagent.lsp.dependencies import dependent_sources
from nextagent.lsp.validation import compare_diagnostics, require_fresh_diagnostics


class DiagnosticGate:
    def __init__(
        self, root: Path, client, *, maximum_severity: int = 2,
        snapshot_text: Callable[[str], str] | None = None,
    ):
        self.root = root.resolve()
        self.client = client
        self.maximum_severity = maximum_severity
        self.snapshot_text = snapshot_text
        self._version = 0

    def _next_version(self) -> int:
        self._version += 1
        return self._version

    def __call__(self, changes: dict[str, tuple[Snapshot, Snapshot]]) -> None:
        """Validate changed files and discovered dependents before SQLite commit.

        With snapshot_text, unchanged dependent documents use the Store's
        latest snapshots rather than stale disk content. Dependency discovery
        scans Store snapshots when supplied, but only for on-disk file paths.
        New unflushed files and non-literal includes are not discovered.
        """
        dependent_names = dependent_sources(
            self.root, set(changes), snapshot_text=self.snapshot_text
        )
        documents: dict[str, tuple[str, str]] = {
            name: (before.text, after.text)
            for name, (before, after) in changes.items()
        }
        for name in sorted(dependent_names):
            if name not in documents:
                path = (self.root / name).resolve()
                text = (
                    self.snapshot_text(name) if self.snapshot_text is not None
                    else path.read_text(encoding="utf-8")
                )
                documents[name] = (text, text)

        opened: list[str] = []
        versions: dict[str, int] = {}
        baseline: dict[str, list[dict]] = {}
        try:
            for name, (before, _) in documents.items():
                path = (self.root / name).resolve()
                if not path.is_relative_to(self.root):
                    raise ValueError("path escapes workspace")
                version = self._next_version()
                uri = self.client.open_document(path, before, version)
                opened.append(uri)
                versions[uri] = version

            for uri in opened:
                version = versions[uri]
                baseline[uri] = require_fresh_diagnostics(
                    self.client.wait_diagnostics(uri, version), version
                )

            for uri, (_, after) in zip(opened, documents.values(), strict=True):
                version = self._next_version()
                self.client.change_document(uri, after, version)
                versions[uri] = version

            for uri in opened:
                version = versions[uri]
                candidate = require_fresh_diagnostics(
                    self.client.wait_diagnostics(uri, version), version
                )
                delta = compare_diagnostics(
                    baseline[uri], candidate, maximum_severity=self.maximum_severity
                )
                if not delta.acceptable:
                    raise ValueError(f"diagnostic_regression: {delta.added}")
        finally:
            for uri in reversed(opened):
                self.client.notify("textDocument/didClose", {"textDocument": {"uri": uri}})
