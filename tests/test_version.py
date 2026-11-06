import tomllib
from pathlib import Path

from aed import __version__


def test_version_matches_package_metadata():
    root = Path(__file__).resolve().parents[1]
    project = tomllib.loads((root / "pyproject.toml").read_text(encoding="utf-8"))
    assert project["project"]["version"] == __version__
