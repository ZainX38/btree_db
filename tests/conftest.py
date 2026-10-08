"""Shared pytest fixtures: every test drives the real binary through a subprocess."""

import os
import pathlib
import subprocess

import pytest

ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_BIN = ROOT / "build" / "debug" / "btree_db"
PROMPT = "db > "
# Sanitizer builds are slow; this only exists so a hung REPL fails the test.
TIMEOUT_SECONDS = 120


@pytest.fixture
def db_bin():
    # BTREE_DB_BIN lets the benchmark or a CI job point tests at another build.
    path = pathlib.Path(os.environ.get("BTREE_DB_BIN", DEFAULT_BIN))
    assert path.exists(), f"binary not built: {path} (run make)"
    return str(path)


@pytest.fixture
def db_path(tmp_path):
    return tmp_path / "test.db"


@pytest.fixture
def repl(db_bin, db_path):
    """Return run(commands) -> stdout lines.

    Each call is a fresh process on the same database file, so calling it twice
    in one test simulates a restart.
    """

    def run(commands):
        proc = subprocess.run(
            [db_bin, str(db_path)],
            input="".join(c + "\n" for c in commands),
            capture_output=True,
            text=True,
            timeout=TIMEOUT_SECONDS,
        )
        # A sanitizer report lands on stderr, so any stderr output is a failure.
        assert proc.stderr == "", proc.stderr
        assert proc.returncode == 0
        return proc.stdout.split("\n")

    return run


LEAF_NODE_MAX_CELLS = 13


def select_rows(repl):
    """Run a fresh session that only selects, and return the printed rows."""
    out = repl(["select", ".exit"])
    lines = [line.removeprefix(PROMPT) for line in out]
    lines = [line for line in lines if line]
    assert lines[-1] == "Executed."
    return lines[:-1]


def btree(repl, setup=()):
    """Run `setup` commands, then .btree, and return just the tree lines."""
    out = repl(list(setup) + [".btree", ".exit"])
    start = out.index(PROMPT + "Tree:")
    return out[start + 1 : -1]
