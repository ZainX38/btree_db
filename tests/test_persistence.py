from conftest import PROMPT

# The tree is a single leaf until splits exist, so 13 rows fill the table.
LEAF_NODE_MAX_CELLS = 13


def select_rows(repl):
    """Run a fresh session that only selects, and return the printed rows."""
    out = repl(["select", ".exit"])
    lines = [line.removeprefix(PROMPT) for line in out]
    lines = [line for line in lines if line]
    assert lines[-1] == "Executed."
    return lines[:-1]


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
    repl([f"insert {i} u{i} e{i}" for i in range(LEAF_NODE_MAX_CELLS)] + [".exit"])
    out = repl(["insert 9999 late l@x", ".exit"])
    assert out[0] == PROMPT + "Error: Table full."
