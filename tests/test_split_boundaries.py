"""Where the new key lands in a full leaf decides which half it ends up in.

These cover the edges of the split loop, where off-by-one mistakes hide:
the first cell, the last cell of the left half, the first cell of the right
half, and the very end.
"""

import pytest

from conftest import LEAF_NODE_MAX_CELLS, btree, select_rows

LEFT_SPLIT_COUNT = (LEAF_NODE_MAX_CELLS + 1) - (LEAF_NODE_MAX_CELLS + 1) // 2
EXISTING_KEYS = [10 * (i + 1) for i in range(LEAF_NODE_MAX_CELLS)]  # 10, 20, ..., 130


def insert(i):
    return f"insert {i} u{i} e{i}"


def leaf_lines(page, keys):
    return [f"  - leaf (page {page}, size {len(keys)})"] + [f"    - {k}" for k in keys]


@pytest.mark.parametrize(
    "new_key, position",
    [(5, 0), (65, 6), (75, 7), (135, 13)],
    ids=["first", "end-of-left-half", "start-of-right-half", "last"],
)
def test_new_key_position_in_split(repl, new_key, position):
    all_keys = sorted(EXISTING_KEYS + [new_key])
    assert all_keys.index(new_key) == position  # the test really hits that edge
    left, right = all_keys[:LEFT_SPLIT_COUNT], all_keys[LEFT_SPLIT_COUNT:]

    tree = btree(repl, [insert(k) for k in EXISTING_KEYS + [new_key]])
    assert tree == (
        ["- internal (page 0, size 1)"]
        + leaf_lines(2, left)
        + [f"  - key {left[-1]}"]
        + leaf_lines(1, right)
    )
    # The row bytes moved with their keys, not just the keys themselves.
    assert select_rows(repl) == [f"({k}, u{k}, e{k})" for k in all_keys]


def test_inserts_after_split_go_to_the_correct_leaf(repl):
    # 7 is the root key: anything <= 7 belongs left, anything above goes right.
    setup = [insert(k) for k in range(1, LEAF_NODE_MAX_CELLS + 2)]
    tree = btree(repl, setup + [insert(0), insert(100)])
    assert tree[1] == "  - leaf (page 2, size 8)"
    assert tree[2] == "    - 0"
    assert tree[-1] == "    - 100"
