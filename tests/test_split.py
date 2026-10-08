"""Leaf splits and root splits."""

import struct

from conftest import LEAF_NODE_MAX_CELLS, PROMPT, btree, select_rows

PAGE_SIZE = 4096
NODE_INTERNAL = 0
NODE_LEAF = 1
IS_ROOT_OFFSET = 1
# Internal header: num_keys at 6, right_child at 10; first cell is child(4) key(4).
INTERNAL_NUM_KEYS_OFFSET = 6
INTERNAL_RIGHT_CHILD_OFFSET = 10
INTERNAL_FIRST_CELL_OFFSET = 14


def insert(i):
    return f"insert {i} u{i} e{i}"


def row(i):
    return f"({i}, u{i}, e{i})"


def leaf_lines(page, keys, level=1):
    pad = "  " * level
    return [f"{pad}- leaf (page {page}, size {len(keys)})"] + [f"{pad}  - {k}" for k in keys]


def test_fourteenth_insert_splits_the_root(repl):
    keys = list(range(1, LEAF_NODE_MAX_CELLS + 2))
    # The new right leaf takes page 1, then the old root's contents move to page 2.
    assert btree(repl, [insert(k) for k in keys]) == (
        ["- internal (page 0, size 1)"]
        + leaf_lines(2, keys[:7])
        + ["  - key 7"]
        + leaf_lines(1, keys[7:])
    )


def test_page_zero_becomes_the_internal_root(repl, db_path):
    repl([insert(k) for k in range(1, LEAF_NODE_MAX_CELLS + 2)] + [".exit"])
    data = db_path.read_bytes()
    assert len(data) == 3 * PAGE_SIZE
    assert data[0] == NODE_INTERNAL
    assert data[IS_ROOT_OFFSET] == 1
    assert struct.unpack_from("<I", data, INTERNAL_NUM_KEYS_OFFSET)[0] == 1
    assert struct.unpack_from("<I", data, INTERNAL_RIGHT_CHILD_OFFSET)[0] == 1
    child, key = struct.unpack_from("<II", data, INTERNAL_FIRST_CELL_OFFSET)
    assert (child, key) == (2, 7)
    # Both children are leaves and neither is the root any more.
    for page in (1, 2):
        assert data[page * PAGE_SIZE] == NODE_LEAF
        assert data[page * PAGE_SIZE + IS_ROOT_OFFSET] == 0


def test_select_walks_every_leaf_in_order(repl):
    keys = list(range(300, 0, -1))
    repl([insert(k) for k in keys] + [".exit"])
    assert select_rows(repl) == [row(k) for k in sorted(keys)]


def test_split_tree_survives_a_restart(repl):
    keys = list(range(1, LEAF_NODE_MAX_CELLS + 2))
    repl([insert(k) for k in keys] + [".exit"])
    repl([insert(100), ".exit"])
    assert select_rows(repl) == [row(k) for k in keys + [100]]


def test_duplicates_are_found_in_either_leaf(repl):
    repl([insert(k) for k in range(1, LEAF_NODE_MAX_CELLS + 2)] + [".exit"])
    out = repl([insert(3), insert(12), ".exit"])
    assert out[:2] == [PROMPT + "Error: Duplicate key 3.", PROMPT + "Error: Duplicate key 12."]


def test_right_most_leaf_split_adds_a_new_right_child(repl):
    keys = list(range(1, 22))  # the 21st insert splits the right-most leaf
    assert btree(repl, [insert(k) for k in keys]) == (
        ["- internal (page 0, size 2)"]
        + leaf_lines(2, keys[:7])
        + ["  - key 7"]
        + leaf_lines(1, keys[7:14])
        + ["  - key 14"]
        + leaf_lines(3, keys[14:])
    )


def test_middle_leaf_split_updates_the_parent_key(repl):
    # Leave gaps so the left leaf can fill up and split while a right leaf exists.
    tens = [10 * i for i in range(1, LEAF_NODE_MAX_CELLS + 2)]  # 10..140, splits the root
    small = list(range(1, 8))  # fills the left leaf (10..70) and splits it
    assert btree(repl, [insert(k) for k in tens + small]) == (
        ["- internal (page 0, size 2)"]
        + leaf_lines(2, small)
        + ["  - key 7"]
        + leaf_lines(3, tens[:7])
        + ["  - key 70"]
        + leaf_lines(1, tens[7:])
    )
    assert select_rows(repl) == [row(k) for k in small + tens]


def test_hundreds_of_rows_survive_a_restart(repl):
    keys = list(range(0, 1500, 3))
    repl([insert(k) for k in keys] + [".exit"])
    out = repl([insert(k) for k in (1, 2, 1499)] + [".exit"])
    assert out[:3] == [PROMPT + "Executed."] * 3
    assert select_rows(repl) == [row(k) for k in sorted(keys + [1, 2, 1499])]
