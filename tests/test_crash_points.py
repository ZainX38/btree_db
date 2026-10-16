"""Crashes the REPL at every single disk write a script makes, then recovers.

A randomly timed kill rarely lands inside the few writes of a split, which is
where a recovery bug would show. BTREE_DB_CRASH_AFTER=N (see crashpoint.h)
lets N writes through and kills the process on the next one, so trying
N = 0, 1, 2, ... until the script finishes covers every crash point in order.

The script runs on the deep-tree build with the smallest allowed cache, so
statements split nodes at several levels and evict dirty pages halfway
through, and it ends with .exit so the final checkpoint is crashed too.
"""

import os
import random
import subprocess

from conftest import (
    PROMPT,
    SMALL_FANOUT_BIN,
    TIMEOUT_SECONDS,
    VALID_TREE,
    binary_args,
    find_binary,
    make_killer,
    make_runner,
    select_rows,
)

CRASH_AFTER_ENV = "BTREE_DB_CRASH_AFTER"
CRASH_EXIT_CODE = 86
MIN_CACHE_FRAMES = 4
BASE_KEYS = list(range(0, 120, 2))
SCRIPT_KEYS = random.Random(86).sample(range(1, 120, 2), 20)
# On the small fan-out build, adding key 286 to keys 0..285 splits a leaf, its
# parent and every node above it up to the root: 32 pages in one statement.
SPLIT_ALL_LEVELS_BASE = list(range(286))
SPLIT_ALL_LEVELS_KEY = 286


def insert(i):
    return f"insert {i} u{i} e{i}"


def row(i):
    return f"({i}, u{i}, e{i})"


def wal_path(db_path):
    return db_path.with_name(db_path.name + ".wal")


def run_with_crash(binary, db_path, commands, crash_after):
    """Run `commands`, crashing after `crash_after` writes.

    Returns (crashed, acknowledged inserts).
    """
    proc = subprocess.run(
        binary_args(binary, db_path, MIN_CACHE_FRAMES, sync=False),
        input="".join(c + "\n" for c in commands),
        env={**os.environ, CRASH_AFTER_ENV: str(crash_after)},
        capture_output=True,
        text=True,
        timeout=TIMEOUT_SECONDS,
    )
    assert proc.stderr == ""
    assert proc.returncode in (0, CRASH_EXIT_CODE)
    return proc.returncode == CRASH_EXIT_CODE, proc.stdout.count("Executed.")


def assert_holds_a_prefix(repl, base, sent, acknowledged):
    """The database holds the base rows plus sent[:m], for some m >= acknowledged."""
    assert repl([".validate", ".exit"])[0] == PROMPT + VALID_TREE
    rows = select_rows(repl)
    m = len(rows) - len(base)
    assert m >= acknowledged, "an acknowledged insert was lost"
    assert rows == [row(k) for k in sorted(base + sent[:m])], "not a prefix of the script"


class Snapshot:
    """Saves the database and its log, so every crash starts from the same state."""

    def __init__(self, db_path):
        self.db_path = db_path
        self.db = db_path.read_bytes()
        self.wal = wal_path(db_path).read_bytes() if wal_path(db_path).exists() else b""

    def restore(self):
        self.db_path.write_bytes(self.db)
        wal_path(self.db_path).write_bytes(self.wal)


def crash_at_every_write(binary, db_path, commands, check):
    """Restore, crash after 0, 1, 2, ... writes and check, until nothing crashes."""
    snapshot = Snapshot(db_path)
    crash_after = 0
    while True:
        snapshot.restore()
        crashed, acknowledged = run_with_crash(binary, db_path, commands, crash_after)
        if not crashed:
            return crash_after
        check(acknowledged)
        crash_after += 1


def test_crash_at_every_write_of_an_insert_script(db_path):
    binary = find_binary("BTREE_DB_SMALL_FANOUT_BIN", SMALL_FANOUT_BIN)
    repl = make_runner(binary, db_path, MIN_CACHE_FRAMES)
    repl([insert(k) for k in BASE_KEYS] + [".exit"])

    def check(acknowledged):
        assert_holds_a_prefix(repl, BASE_KEYS, SCRIPT_KEYS, acknowledged)

    script = [insert(k) for k in SCRIPT_KEYS] + [".exit"]
    writes = crash_at_every_write(binary, db_path, script, check)
    # Splits and the final checkpoint make far more writes than inserts.
    assert writes > 3 * len(SCRIPT_KEYS)


def test_crash_at_every_write_of_recovery(db_path):
    binary = find_binary("BTREE_DB_SMALL_FANOUT_BIN", SMALL_FANOUT_BIN)
    repl = make_runner(binary, db_path, MIN_CACHE_FRAMES)
    repl([insert(k) for k in BASE_KEYS] + [".exit"])
    # Killed after the script, so every insert is committed in the log but
    # none is checkpointed: the next start has a full recovery to do.
    make_killer(binary, db_path, MIN_CACHE_FRAMES)([insert(k) for k in SCRIPT_KEYS])

    def check(_):
        # A crash during recovery must not lose anything: the log is only
        # emptied once its pages are in the database file.
        assert_holds_a_prefix(repl, BASE_KEYS, SCRIPT_KEYS, len(SCRIPT_KEYS))

    writes = crash_at_every_write(binary, db_path, [".exit"], check)
    assert writes > 0


def test_crash_at_every_write_of_a_split_through_every_level(db_path):
    # Freshly checkpointed, so no page this statement touches has an image in
    # the log yet. If a half-done statement ever reached the database file,
    # recovery would have no committed image to put back over it.
    binary = find_binary("BTREE_DB_SMALL_FANOUT_BIN", SMALL_FANOUT_BIN)
    repl = make_runner(binary, db_path, MIN_CACHE_FRAMES)
    repl([insert(k) for k in SPLIT_ALL_LEVELS_BASE] + [".exit"])

    def check(acknowledged):
        assert_holds_a_prefix(repl, SPLIT_ALL_LEVELS_BASE, [SPLIT_ALL_LEVELS_KEY], acknowledged)

    script = [insert(SPLIT_ALL_LEVELS_KEY), ".exit"]
    writes = crash_at_every_write(binary, db_path, script, check)
    # Each logged page is two writes (header, image), plus its checkpoint copy.
    assert writes > 60
