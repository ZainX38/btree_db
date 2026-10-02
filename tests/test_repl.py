from conftest import PROMPT


def test_exit_meta_command(repl):
    assert repl([".exit"]) == [PROMPT]


def test_end_of_input_exits_cleanly(repl):
    assert repl([]) == [PROMPT]


def test_unrecognized_meta_command(repl):
    out = repl([".foo", ".exit"])
    assert out[0] == PROMPT + "Unrecognized command '.foo'."


def test_unrecognized_keyword(repl):
    out = repl(["hello", ".exit"])
    assert out[0] == PROMPT + "Unrecognized keyword at start of 'hello'."


def test_empty_lines_are_ignored(repl):
    assert repl(["", "", ".exit"]) == [PROMPT * 3]
