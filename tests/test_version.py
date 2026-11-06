import re
from pathlib import Path

from nextagent import __version__


def test_project_version_consistency():
    project = Path(__file__).resolve().parents[1]
    pyproject = (project / "pyproject.toml").read_text(encoding="utf-8")
    assert re.search(r'^version = "' + re.escape(__version__) + r'"$', pyproject, re.M)
    changelog = (project / "CHANGELOG.md").read_text(encoding="utf-8")
    assert f"## {__version__} " in changelog
