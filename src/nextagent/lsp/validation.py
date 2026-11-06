"""Conservative diagnostic comparison for candidate edits.

A diagnostic gate must not treat missing/stale LSP diagnostics as success.
"""
from collections import Counter
from dataclasses import dataclass
from typing import Any


@dataclass(frozen=True, slots=True)
class DiagnosticDelta:
    added: tuple[tuple[int, str, str], ...]
    removed: tuple[tuple[int, str, str], ...]
    acceptable: bool


def _fingerprint(diagnostic: dict[str, Any]) -> tuple[int, str, str]:
    severity = diagnostic.get("severity", 1)
    if isinstance(severity, bool) or not isinstance(severity, int):
        raise ValueError("invalid diagnostic severity")
    return severity, str(diagnostic.get("code", "")), str(diagnostic.get("message", ""))


def compare_diagnostics(
    before: list[dict[str, Any]], after: list[dict[str, Any]],
    *, maximum_severity: int = 2,
) -> DiagnosticDelta:
    """Reject newly introduced errors/warnings (LSP: 1 error, 2 warning).

    Compare multisets to preserve duplicate counts; positions are deliberately
    excluded because edits move source ranges.
    """
    if maximum_severity not in (1, 2, 3, 4):
        raise ValueError("invalid severity threshold")
    previous = Counter(_fingerprint(item) for item in before)
    current = Counter(_fingerprint(item) for item in after)
    added = tuple(sorted((current - previous).elements()))
    removed = tuple(sorted((previous - current).elements()))
    return DiagnosticDelta(
        added, removed, not any(severity <= maximum_severity for severity, _, _ in added)
    )


def require_fresh_diagnostics(
    report: dict[str, Any] | None, expected_version: int
) -> list[dict[str, Any]]:
    """Fail closed if clangd has not published diagnostics for the exact revision."""
    if report is None or report.get("version") != expected_version:
        raise ValueError("diagnostics_unavailable_or_stale")
    diagnostics = report.get("diagnostics")
    if not isinstance(diagnostics, list) or not all(
        isinstance(item, dict) for item in diagnostics
    ):
        raise ValueError("invalid diagnostics payload")
    return diagnostics
