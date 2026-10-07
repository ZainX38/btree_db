"""Single-leaf B+tree behaviour: sorted cells and duplicate keys."""

import random

from conftest import LEAF_NODE_MAX_CELLS, PROMPT, select_rows


def insert(i):
    return f"insert {i} u{i} e{i}"


def row(i):
    return f"({i}, u{i}, e{i})"


def test_reverse_inserts_come_back_sorted(repl):
    keys = list(range(LEAF_NODE_MAX_CELLS, 0, -1))
    repl([insert(k) for k in keys] + [".exit"])
    assert select_rows(repl) == [row(k) for k in sorted(keys)]


def test_random_inserts_come_back_sorted(repl):
    keys = random.Random(42).sample(range(1_000_000), LEAF_NODE_MAX_CELLS)
    repl([insert(k) for k in keys] + [".exit"])
    assert select_rows(repl) == [row(k) for k in sorted(keys)]


def test_insert_at_front_middle_and_end(repl):
    # Each insert lands at a different edge of the binary search.
    repl([insert(k) for k in (20, 40, 10, 30, 50)] + [".exit"])
    assert select_rows(repl) == [row(k) for k in (10, 20, 30, 40, 50)]


def test_duplicate_key_is_rejected(repl):
    out = repl([insert(1), "insert 1 other o@x", ".exit"])
    assert out[1] == PROMPT + "Error: Duplicate key 1."
    assert select_rows(repl) == [row(1)]  # the original row is untouched


def test_duplicate_key_is_rejected_after_restart(repl):
    repl([insert(5), ".exit"])
    out = repl([insert(5), ".exit"])
    assert out[0] == PROMPT + "Error: Duplicate key 5."


def test_duplicate_is_reported_even_when_leaf_is_full(repl):
    repl([insert(k) for k in range(LEAF_NODE_MAX_CELLS)] + [".exit"])
    out = repl([insert(0), insert(999), ".exit"])
    assert out[0] == PROMPT + "Error: Duplicate key 0."
    assert out[1] == PROMPT + "Executed."  # a full root leaf now splits instead


def test_smallest_and_largest_keys(repl):
    repl([insert(4294967295), insert(0), ".exit"])
    assert select_rows(repl) == [row(0), row(4294967295)]
