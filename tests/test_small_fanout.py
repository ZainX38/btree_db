"""Deep trees and internal splits, using the small fan-out binary (3 keys per node)."""

import random

from conftest import SMALL_FANOUT_MAX_KEYS, btree, select_rows

# Sequential inserts leave every split-off leaf with 7 rows and fill the last
# one to 13, so a root with 3 keys (4 leaves) is full at 7 * 3 + 13 rows.
ROWS_UNTIL_ROOT_IS_FULL = 7 * SMALL_FANOUT_MAX_KEYS + 13


def insert(i):
    return f"insert {i} u{i} e{i}"


def row(i):
    return f"({i}, u{i}, e{i})"


def leaf_lines(page, keys, level):
    pad = "  " * level
    return [f"{pad}- leaf (page {page}, size {len(keys)})"] + [f"{pad}  - {k}" for k in keys]


def test_root_fills_up_after_three_keys(small_fanout_repl):
    tree = btree(small_fanout_repl, [insert(k) for k in range(ROWS_UNTIL_ROOT_IS_FULL)])
    assert tree[0] == f"- internal (page 0, size {SMALL_FANOUT_MAX_KEYS})"
    assert sum(1 for line in tree if line.startswith("  - leaf")) == SMALL_FANOUT_MAX_KEYS + 1


def test_full_internal_root_splits_into_three_levels(small_fanout_repl):
    # Worked out by hand: leaves are created on pages 1-5, the root's new right
    # half on page 6, and the old root's contents are copied to page 7.
    keys = list(range(ROWS_UNTIL_ROOT_IS_FULL + 1))
    assert btree(small_fanout_repl, [insert(k) for k in keys]) == (
        ["- internal (page 0, size 1)", "  - internal (page 7, size 2)"]
        + leaf_lines(2, keys[0:7], 2)
        + ["    - key 6"]
        + leaf_lines(1, keys[7:14], 2)
        + ["    - key 13"]
        + leaf_lines(3, keys[14:21], 2)
        + ["  - key 20", "  - internal (page 6, size 1)"]
        + leaf_lines(4, keys[21:28], 2)
        + ["    - key 27"]
        + leaf_lines(5, keys[28:35], 2)
    )


def test_sequential_inserts_build_a_deep_tree(small_fanout_repl):
    keys = list(range(600))
    small_fanout_repl([insert(k) for k in keys] + [".exit"])
    assert select_rows(small_fanout_repl) == [row(k) for k in keys]


def test_reverse_inserts_build_a_deep_tree(small_fanout_repl):
    keys = list(range(600, 0, -1))
    small_fanout_repl([insert(k) for k in keys] + [".exit"])
    assert select_rows(small_fanout_repl) == [row(k) for k in sorted(keys)]


def test_random_inserts_build_a_deep_tree(small_fanout_repl):
    keys = random.Random(7).sample(range(100_000), 800)
    small_fanout_repl([insert(k) for k in keys] + [".exit"])
    assert select_rows(small_fanout_repl) == [row(k) for k in sorted(keys)]


def test_deep_tree_survives_restarts(small_fanout_repl):
    keys = random.Random(11).sample(range(100_000), 600)
    for chunk_start in range(0, len(keys), 200):  # three separate sessions
        small_fanout_repl([insert(k) for k in keys[chunk_start : chunk_start + 200]] + [".exit"])
    assert select_rows(small_fanout_repl) == [row(k) for k in sorted(keys)]
