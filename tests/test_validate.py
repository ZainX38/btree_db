"""The .validate meta command: valid trees pass, and corrupted files are caught."""

import random
import struct

import pytest

from conftest import LEAF_NODE_MAX_CELLS, PROMPT

PAGE_SIZE = 4096
PARENT_OFFSET = 2
NEXT_LEAF_OFFSET = 10
LEAF_HEADER_SIZE = 14
LEAF_CELL_SIZE = 4 + 293
# After 14 sequential inserts: root on page 0, left leaf on page 2, right leaf on page 1.
LEFT_LEAF_PAGE = 2
RIGHT_LEAF_PAGE = 1


def insert(i):
    return f"insert {i} u{i} e{i}"


def validate(repl, setup=()):
    out = repl(list(setup) + [".validate", ".exit"])
    return out[len(setup)].removeprefix(PROMPT)


def test_empty_tree_is_valid(repl):
    assert validate(repl) == "Tree is valid."


def test_split_tree_is_valid(repl):
    assert validate(repl, [insert(k) for k in range(LEAF_NODE_MAX_CELLS + 1)]) == "Tree is valid."


@pytest.mark.parametrize("order", ["sequential", "reverse", "random"])
def test_deep_trees_are_valid(small_fanout_repl, order):
    keys = list(range(500))
    if order == "reverse":
        keys.reverse()
    elif order == "random":
        random.Random(3).shuffle(keys)
    assert validate(small_fanout_repl, [insert(k) for k in keys]) == "Tree is valid."


def build_split_tree(repl, db_path):
    repl([insert(k) for k in range(LEAF_NODE_MAX_CELLS + 1)] + [".exit"])
    return bytearray(db_path.read_bytes())


def write_u32(data, offset, value):
    struct.pack_into("<I", data, offset, value)


def test_keys_out_of_order_are_caught(repl, db_path):
    data = build_split_tree(repl, db_path)
    first_key = LEFT_LEAF_PAGE * PAGE_SIZE + LEAF_HEADER_SIZE
    second_key = first_key + LEAF_CELL_SIZE
    data[first_key : first_key + 4], data[second_key : second_key + 4] = (
        data[second_key : second_key + 4],
        data[first_key : first_key + 4],
    )
    db_path.write_bytes(data)
    assert validate(repl) == f"Invalid tree: page {LEFT_LEAF_PAGE}: keys out of order."


def test_wrong_parent_pointer_is_caught(repl, db_path):
    data = build_split_tree(repl, db_path)
    write_u32(data, RIGHT_LEAF_PAGE * PAGE_SIZE + PARENT_OFFSET, 5)
    db_path.write_bytes(data)
    assert validate(repl) == f"Invalid tree: page {RIGHT_LEAF_PAGE}: parent pointer is wrong."


def test_broken_leaf_chain_is_caught(repl, db_path):
    data = build_split_tree(repl, db_path)
    write_u32(data, LEFT_LEAF_PAGE * PAGE_SIZE + NEXT_LEAF_OFFSET, 0)
    db_path.write_bytes(data)
    assert validate(repl) == (
        f"Invalid tree: page {LEFT_LEAF_PAGE}: next_leaf does not point at the next leaf."
    )
