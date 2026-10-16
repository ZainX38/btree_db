"""Kills the REPL with SIGKILL while it is busy inserting, then checks what survived.

Inserts are streamed in faster than the REPL can answer, and the process is
killed at a random point after some of them have been acknowledged, so the
kill lands in the middle of a statement: a split, an eviction, a WAL append
or a checkpoint. Sync is on, as it would be for a real user.

After every crash the restarted database must hold every acknowledged row,
and the rows it holds must be exactly the first m inserts sent, for some m:
statements run one at a time, so a later insert can never survive while an
earlier one is lost, and none can be half applied.
"""

import random
import subprocess
import threading

from conftest import (
    PROMPT,
    SMALL_FANOUT_BIN,
    TIMEOUT_SECONDS,
    TINY_CACHE_FRAMES,
    VALID_TREE,
    binary_args,
    find_binary,
    make_runner,
    select_rows,
)

ROUNDS = 8
INSERTS_PER_ROUND = 300
ACKNOWLEDGED = "Executed."


def insert(i):
    return f"insert {i} u{i} e{i}"


def row(i):
    return f"({i}, u{i}, e{i})"


def insert_then_kill(args, keys, kill_after):
    """Stream inserts for `keys`, SIGKILL once `kill_after` are acknowledged.

    Returns how many inserts were acknowledged before the kill.
    """
    proc = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True)

    def feed():
        try:
            proc.stdin.write("".join(insert(k) + "\n" for k in keys))
            proc.stdin.flush()
        except BrokenPipeError:
            pass  # killed before it read everything, which is the point

    threading.Thread(target=feed, daemon=True).start()
    watchdog = threading.Timer(TIMEOUT_SECONDS, proc.kill)
    watchdog.start()
    acknowledged = 0
    for line in proc.stdout:
        assert line.removeprefix(PROMPT).strip() == ACKNOWLEDGED, line
        acknowledged += 1
        if acknowledged == kill_after:
            break
    proc.kill()
    proc.wait()
    watchdog.cancel()
    assert acknowledged == kill_after, "REPL stopped answering before the kill"
    assert proc.stderr.read() == ""
    return acknowledged


def surviving_prefix(survivors, sent):
    """Return m if `survivors` is exactly sent[:m], else fail."""
    m = len(survivors)
    assert survivors == set(sent[:m]), "rows survived out of order or half applied"
    return m


def test_crash_mid_write_keeps_every_acknowledged_row(db_path):
    binary = find_binary("BTREE_DB_SMALL_FANOUT_BIN", SMALL_FANOUT_BIN)
    args = binary_args(binary, db_path, TINY_CACHE_FRAMES, sync=True)
    repl = make_runner(binary, db_path, TINY_CACHE_FRAMES)
    rng = random.Random(28)
    keys = rng.sample(range(1_000_000), ROUNDS * INSERTS_PER_ROUND)
    committed = []  # keys known to be in the database, in insert order

    for round_num in range(ROUNDS):
        sent = keys[round_num * INSERTS_PER_ROUND : (round_num + 1) * INSERTS_PER_ROUND]
        acknowledged = insert_then_kill(args, sent, rng.randrange(1, INSERTS_PER_ROUND))

        out = repl([".validate", ".exit"])
        assert out[0] == PROMPT + VALID_TREE
        rows = select_rows(repl)
        survivors = {int(r[1:].split(",")[0]) for r in rows} - set(committed)
        m = surviving_prefix(survivors, sent)
        assert m >= acknowledged, "an acknowledged insert was lost"
        committed += sent[:m]
        assert rows == [row(k) for k in sorted(committed)]
