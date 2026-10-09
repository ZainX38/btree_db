"""Trees built by the small fan-out binary (internal nodes hold 3 keys)."""

from conftest import PROMPT, SMALL_FANOUT_MAX_KEYS, btree, select_rows

# Sequential inserts leave every split-off leaf with 7 rows and fill the last
# one to 13, so a root with 3 keys (4 leaves) is full at 7 * 3 + 13 rows.
ROWS_UNTIL_ROOT_IS_FULL = 7 * SMALL_FANOUT_MAX_KEYS + 13


def insert(i):
    return f"insert {i} u{i} e{i}"


def test_root_fills_up_after_three_keys(small_fanout_repl):
    tree = btree(small_fanout_repl, [insert(k) for k in range(ROWS_UNTIL_ROOT_IS_FULL)])
    assert tree[0] == f"- internal (page 0, size {SMALL_FANOUT_MAX_KEYS})"
    assert sum(1 for line in tree if line.startswith("  - leaf")) == SMALL_FANOUT_MAX_KEYS + 1


def test_full_internal_node_is_reported(small_fanout_repl):
    # Internal nodes can't split yet, so the next leaf split has nowhere to go.
    out = small_fanout_repl([insert(k) for k in range(ROWS_UNTIL_ROOT_IS_FULL + 1)] + [".exit"])
    assert out[ROWS_UNTIL_ROOT_IS_FULL - 1] == PROMPT + "Executed."
    assert out[ROWS_UNTIL_ROOT_IS_FULL] == PROMPT + "Error: Table full."
    # The refused insert left the tree exactly as it was.
    assert select_rows(small_fanout_repl) == [
        f"({k}, u{k}, e{k})" for k in range(ROWS_UNTIL_ROOT_IS_FULL)
    ]
