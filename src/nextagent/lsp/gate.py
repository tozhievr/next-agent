"""Pre-commit clangd diagnostic gate for Store.edit.

No Store revision is committed until the candidate has passed validation.
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

    def __call__(self, changes: dict[str, tuple[Snapshot, Snapshot]]) -> None:
        """Validate candidates using a versioned clangd overlay, without writing files.

        Fail closed on missing diagnostics or newly introduced errors/warnings.
        The Store owns the SQLite transaction and rolls back on any exception.
        """
        for name, (before, after) in changes.items():
            path = (self.root / name).resolve()
            if not path.is_relative_to(self.root):
                raise ValueError("path escapes workspace")
            self._version += 1
            baseline_version = self._version
            uri = self.client.open_document(path, before.text, baseline_version)
            try:
                baseline = require_fresh_diagnostics(
                    self.client.wait_diagnostics(uri, baseline_version), baseline_version
                )
                self._version += 1
                candidate_version = self._version
                self.client.change_document(uri, after.text, candidate_version)
                candidate = require_fresh_diagnostics(
                    self.client.wait_diagnostics(uri, candidate_version), candidate_version
                )
                delta = compare_diagnostics(
                    baseline, candidate, maximum_severity=self.maximum_severity
                )
                if not delta.acceptable:
                    raise ValueError(f"diagnostic_regression: {delta.added}")
            finally:
                # Do not leave an uncommitted candidate visible to the LSP server.
                self.client.notify("textDocument/didClose", {"textDocument": {"uri": uri}})
