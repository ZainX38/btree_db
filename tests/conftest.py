"""Shared pytest fixtures: every test drives the real binary through a subprocess."""

import os
import pathlib
import subprocess

import pytest

ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_BIN = ROOT / "build" / "debug" / "btree_db"
# Same code built with INTERNAL_NODE_MAX_KEYS=3 (see the Makefile).
SMALL_FANOUT_BIN = ROOT / "build" / "small_fanout" / "btree_db"
SMALL_FANOUT_MAX_KEYS = 3
PROMPT = "db > "
# Sanitizer builds are slow; this only exists so a hung REPL fails the test.
TIMEOUT_SECONDS = 120

LEAF_NODE_MAX_CELLS = 13
# A cache far smaller than the test databases, so pages are evicted constantly.
TINY_CACHE_FRAMES = 8


def find_binary(env_var, default):
    # The env var lets the benchmark or a CI job point tests at another build.
    path = pathlib.Path(os.environ.get(env_var, default))
    assert path.exists(), f"binary not built: {path} (run make test)"
    return str(path)


def make_runner(binary, db_path, cache_frames=None):
    """Return run(commands) -> stdout lines.

    Each call is a fresh process on the same database file, so calling it twice
    in one test simulates a restart.
    """
    args = [binary] + ([] if cache_frames is None else ["--cache-frames", str(cache_frames)])

    def run(commands):
        proc = subprocess.run(
            args + [str(db_path)],
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


@pytest.fixture
def db_bin():
    return find_binary("BTREE_DB_BIN", DEFAULT_BIN)


@pytest.fixture
def db_path(tmp_path):
    return tmp_path / "test.db"


@pytest.fixture
def repl(db_bin, db_path):
    return make_runner(db_bin, db_path)


@pytest.fixture
def small_fanout_repl(db_path):
    return make_runner(find_binary("BTREE_DB_SMALL_FANOUT_BIN", SMALL_FANOUT_BIN), db_path)


@pytest.fixture
def tiny_cache_repl(db_bin, db_path):
    return make_runner(db_bin, db_path, TINY_CACHE_FRAMES)


@pytest.fixture
def tiny_cache_small_fanout_repl(db_path):
    binary = find_binary("BTREE_DB_SMALL_FANOUT_BIN", SMALL_FANOUT_BIN)
    return make_runner(binary, db_path, TINY_CACHE_FRAMES)


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
