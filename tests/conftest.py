"""Shared pytest fixtures: every test drives the real binary through a subprocess."""

import os
import pathlib

import pytest

ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_BIN = ROOT / "build" / "debug" / "btree_db"


@pytest.fixture
def db_bin():
    # BTREE_DB_BIN lets the benchmark or a CI job point tests at another build.
    path = pathlib.Path(os.environ.get("BTREE_DB_BIN", DEFAULT_BIN))
    assert path.exists(), f"binary not built: {path} (run make)"
    return str(path)
