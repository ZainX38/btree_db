"""Shared pytest fixtures: every test drives the real binary through a subprocess."""

import os
import pathlib
import subprocess
import threading

import pytest

ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_BIN = ROOT / "build" / "debug" / "btree_db"
# Same code built with INTERNAL_NODE_MAX_KEYS=3 (see the Makefile).
SMALL_FANOUT_BIN = ROOT / "build" / "small_fanout" / "btree_db"
SMALL_FANOUT_MAX_KEYS = 3
PROMPT = "db > "
# Sanitizer builds are slow; this only exists so a hung REPL fails the test.
TIMEOUT_SECONDS = 120

PAGE_SIZE_BYTES = 4096
LEAF_NODE_MAX_CELLS = 13
# A cache far smaller than the test databases, so pages are evicted constantly.
TINY_CACHE_FRAMES = 8
VALID_TREE = "Tree is valid."


def find_binary(env_var, default):
    # The env var lets the benchmark or a CI job point tests at another build.
    path = pathlib.Path(os.environ.get(env_var, default))
    assert path.exists(), f"binary not built: {path} (run make test)"
    return str(path)


def binary_args(binary, db_path, cache_frames, sync):
    args = [binary] + ([] if cache_frames is None else ["--cache-frames", str(cache_frames)])
    return args + ([] if sync else ["--no-sync"]) + [str(db_path)]


def make_runner(binary, db_path, cache_frames=None, sync=False):
    """Return run(commands) -> stdout lines.

    Each call is a fresh process on the same database file, so calling it twice
    in one test simulates a restart. Sync is off by default: an fsync per insert
    makes the suite about 15 times slower, and a test process never loses power.
    """
    args = binary_args(binary, db_path, cache_frames, sync)

    def run(commands):
        proc = subprocess.run(
            args,
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


def make_killer(binary, db_path, cache_frames=None, sync=False):
    """Return run_then_kill(commands) -> stdout lines.

    Like make_runner, but the process never exits cleanly: once every command
    has been answered it gets SIGKILL, so nothing runs at shutdown. A trailing
    .validate marks the end, since its answer comes after all the others.
    """
    args = binary_args(binary, db_path, cache_frames, sync)

    def run_then_kill(commands):
        proc = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        # Writing from another thread, because a long script fills the stdin
        # pipe while the REPL is blocked on a full stdout pipe we aren't reading.
        script = "".join(c + "\n" for c in list(commands) + [".validate"])

        def feed():
            try:
                proc.stdin.write(script)
                proc.stdin.flush()
            except BrokenPipeError:
                pass  # killed before reading it all, which is fine

        threading.Thread(target=feed, daemon=True).start()
        watchdog = threading.Timer(TIMEOUT_SECONDS, proc.kill)
        watchdog.start()
        lines = []
        for line in proc.stdout:
            if line.endswith(VALID_TREE + "\n"):
                break
            lines.append(line.rstrip("\n"))
        else:
            pytest.fail("REPL exited or timed out before finishing the script")
        proc.kill()
        proc.wait()
        watchdog.cancel()
        assert proc.stderr.read() == ""
        return lines

    return run_then_kill


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
