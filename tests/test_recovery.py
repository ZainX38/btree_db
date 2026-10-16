"""Crash recovery: kill a session, damage its log the way a crash could, restart.

Every test checks the same promise: a restart brings back exactly the
statements whose commit record reached the log intact, and the tree is valid.
"""

import pytest

from conftest import (
    PROMPT,
    SMALL_FANOUT_BIN,
    TINY_CACHE_FRAMES,
    VALID_TREE,
    find_binary,
    make_killer,
    select_rows,
)
from test_wal import HEADER, RECORD_COMMIT, RECORD_PAGE, record_spans, wal_path

ROWS = 40


def insert(i):
    return f"insert {i} u{i} e{i}"


def row(i):
    return f"({i}, u{i}, e{i})"


@pytest.fixture
def killed_repl(db_bin, db_path):
    return make_killer(db_bin, db_path)


def commit_ends(db_path):
    return [end for kind, _, _, end in record_spans(db_path) if kind == RECORD_COMMIT]


def assert_recovered(repl, keys):
    out = repl([".validate", ".exit"])
    assert out[0] == PROMPT + VALID_TREE
    assert select_rows(repl) == [row(k) for k in sorted(keys)]


def test_killed_session_is_replayed(killed_repl, repl, db_path):
    killed_repl([insert(i) for i in range(ROWS)])
    assert_recovered(repl, range(ROWS))
    assert wal_path(db_path).stat().st_size == 0


def test_recovery_empties_the_log_straight_away(killed_repl, db_path):
    killed_repl([insert(i) for i in range(ROWS)])
    killed_repl(["select"])
    assert wal_path(db_path).stat().st_size == 0


@pytest.mark.parametrize("cut", [1, HEADER.size, HEADER.size + 1, HEADER.size + 2000])
def test_torn_last_statement_is_dropped(killed_repl, repl, db_path, cut):
    killed_repl([insert(i) for i in range(ROWS)])
    path = wal_path(db_path)
    path.write_bytes(path.read_bytes()[:-cut])
    assert_recovered(repl, range(ROWS - 1))


def test_bad_crc_drops_that_statement_and_everything_after(killed_repl, repl, db_path):
    killed_repl([insert(i) for i in range(ROWS)])
    # Commit 0 is the new database's root; commit k + 1 is insert k.
    broken_insert = ROWS // 2
    start = commit_ends(db_path)[broken_insert]
    data = bytearray(wal_path(db_path).read_bytes())
    data[start + HEADER.size + 100] ^= 0xFF  # inside the next page image
    wal_path(db_path).write_bytes(bytes(data))
    assert_recovered(repl, range(broken_insert))


def test_page_images_without_a_commit_are_ignored(killed_repl, repl, db_path):
    killed_repl([insert(i) for i in range(ROWS)])
    data = wal_path(db_path).read_bytes()
    # The root's very first image is an empty leaf: applying it would lose
    # every row, so it proves uncommitted images are never applied.
    kind, page_num, start, end = record_spans(db_path)[0]
    assert (kind, page_num) == (RECORD_PAGE, 0)
    wal_path(db_path).write_bytes(data + data[start:end])
    assert_recovered(repl, range(ROWS))


def test_new_statements_after_a_torn_tail_survive_the_next_crash(killed_repl, repl, db_path):
    killed_repl([insert(i) for i in range(ROWS)])
    path = wal_path(db_path)
    path.write_bytes(path.read_bytes()[:-1])
    killed_repl([insert(i) for i in range(ROWS, 2 * ROWS)])
    assert_recovered(repl, [*range(ROWS - 1), *range(ROWS, 2 * ROWS)])


def test_recovery_with_a_tiny_cache_and_a_deep_tree(db_path, small_fanout_repl):
    binary = find_binary("BTREE_DB_SMALL_FANOUT_BIN", SMALL_FANOUT_BIN)
    keys = list(range(0, 3000, 7))
    make_killer(binary, db_path, TINY_CACHE_FRAMES)([insert(k) for k in reversed(keys)])
    assert_recovered(small_fanout_repl, keys)


@pytest.mark.parametrize("junk", [b"\x01", b"\x01\x00\x00\x00\x00\x00", b"\xff" * 100])
def test_log_with_only_a_torn_record_is_discarded(repl, db_path, junk):
    repl([insert(i) for i in range(ROWS)] + [".exit"])
    wal_path(db_path).write_bytes(junk)
    assert_recovered(repl, range(ROWS))
    assert wal_path(db_path).stat().st_size == 0
