from conftest import LEAF_NODE_MAX_CELLS, MAX_SEQUENTIAL_ROWS, PROMPT, select_rows


def test_rows_survive_a_restart(repl):
    repl(["insert 1 alice a@x", ".exit"])
    assert select_rows(repl) == ["(1, alice, a@x)"]


def test_end_of_input_also_saves(repl):
    repl(["insert 1 alice a@x"])  # no .exit
    assert select_rows(repl) == ["(1, alice, a@x)"]


def test_sessions_append_to_existing_rows(repl):
    repl(["insert 1 alice a@x", ".exit"])
    repl(["insert 2 bob b@x", ".exit"])
    assert select_rows(repl) == ["(1, alice, a@x)", "(2, bob, b@x)"]


def test_full_leaf_survives_a_restart(repl):
    repl([f"insert {i} u{i} e{i}" for i in range(LEAF_NODE_MAX_CELLS)] + [".exit"])
    assert select_rows(repl) == [f"({i}, u{i}, e{i})" for i in range(LEAF_NODE_MAX_CELLS)]


def test_full_table_stays_full_after_restart(repl):
    repl([f"insert {i} u{i} e{i}" for i in range(MAX_SEQUENTIAL_ROWS)] + [".exit"])
    out = repl(["insert 9999 late l@x", ".exit"])
    assert out[0] == PROMPT + "Error: Table full."
