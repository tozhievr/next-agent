"""Pre-commit clangd diagnostic gate for atomic Store edits.

All changed documents are opened together before any candidate overlay is sent.
This allows clangd to observe a coherent multi-document candidate state.
"""
from pathlib import Path

from nextagent.core.snapshot import Snapshot
from nextagent.lsp.validation import compare_diagnostics, require_fresh_diagnostics


class DiagnosticGate:
    def __init__(self, root: Path, client, *, maximum_severity: int = 2):
        self.root = root.resolve()
        self.client = client
        self.maximum_severity = maximum_severity
        self._version = 0

    def _next_version(self) -> int:
        self._version += 1
        return self._version

    def __call__(self, changes: dict[str, tuple[Snapshot, Snapshot]]) -> None:
        """Validate the entire changed-document batch before Store commit.

        Fail closed on missing/stale diagnostics. This is NOT a whole-workspace
        dependency check: unchanged translation units are not opened or tested.
        """
        opened: list[str] = []
        versions: dict[str, int] = {}
        baseline: dict[str, list[dict]] = {}
        try:
            for name, (before, _) in changes.items():
                path = (self.root / name).resolve()
                if not path.is_relative_to(self.root):
                    raise ValueError("path escapes workspace")
                version = self._next_version()
                uri = self.client.open_document(path, before.text, version)
                opened.append(uri)
                versions[uri] = version

            for uri in opened:
                version = versions[uri]
                baseline[uri] = require_fresh_diagnostics(
                    self.client.wait_diagnostics(uri, version), version
                )

            for uri, (_, after) in zip(opened, changes.values(), strict=True):
                version = self._next_version()
                self.client.change_document(uri, after.text, version)
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
            # Close all candidate overlays even if validation fails midway.
            for uri in reversed(opened):
                self.client.notify("textDocument/didClose", {"textDocument": {"uri": uri}})
