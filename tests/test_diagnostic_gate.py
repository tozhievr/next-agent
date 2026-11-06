import pytest

from nextagent.lsp.validation import compare_diagnostics, require_fresh_diagnostics


def _diag(message, severity=1):
    return {"message": message, "severity": severity}


def test_new_error_blocks_candidate():
    delta = compare_diagnostics([_diag("existing")], [
        _diag("existing"), _diag("new")
    ])
    assert not delta.acceptable
    assert delta.added == ((1, "", "new"),)


def test_reordered_and_moved_diagnostics_are_not_new():
    before = [_diag("one"), _diag("two")]
    after = [{"message": "two", "severity": 1, "range": {"start": {"line": 5}}},
             _diag("one")]
    assert compare_diagnostics(before, after).acceptable


def test_duplicate_diagnostics_are_counted():
    delta = compare_diagnostics([_diag("duplicate")], [
        _diag("duplicate"), _diag("duplicate")
    ])
    assert not delta.acceptable


def test_warnings_are_configurable():
    assert not compare_diagnostics([], [_diag("warning", 2)]).acceptable
    assert compare_diagnostics([], [_diag("warning", 2)], maximum_severity=1).acceptable


def test_missing_or_stale_diagnostics_fail_closed():
    for report in (None, {"version": 1, "diagnostics": []},
                   {"version": 2, "diagnostics": "bad"}):
        with pytest.raises(ValueError):
            require_fresh_diagnostics(report, 2)
    assert require_fresh_diagnostics({"version": 2, "diagnostics": []}, 2) == []
