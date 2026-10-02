from conftest import PROMPT


def test_binary_runs_cleanly(repl):
    # The repl fixture already fails on a non-zero exit or any stderr output.
    assert repl([]) == [PROMPT]
