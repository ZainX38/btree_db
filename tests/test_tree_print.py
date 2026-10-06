"""The .btree meta command, used to check the shape of the tree in other tests."""

from conftest import PROMPT


def btree(repl, setup=()):
    """Run `setup` commands, then .btree, and return just the tree lines."""
    out = repl(list(setup) + [".btree", ".exit"])
    start = out.index(PROMPT + "Tree:")
    return out[start + 1 : -1]


def test_empty_tree(repl):
    assert btree(repl) == ["- leaf (page 0, size 0)"]


def test_single_leaf_lists_sorted_keys(repl):
    setup = [f"insert {k} u{k} e{k}" for k in (3, 1, 2)]
    assert btree(repl, setup) == [
        "- leaf (page 0, size 3)",
        "  - 1",
        "  - 2",
        "  - 3",
    ]
