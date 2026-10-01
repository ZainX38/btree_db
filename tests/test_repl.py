import subprocess

PROMPT = "db > "


def run_script(db_bin, commands):
    """Feed commands to the REPL and return its stdout split into lines."""
    proc = subprocess.run(
        [db_bin],
        input="".join(c + "\n" for c in commands),
        capture_output=True,
        text=True,
        timeout=30,
    )
    assert proc.stderr == "", proc.stderr
    assert proc.returncode == 0
    return proc.stdout.split("\n")


def test_exit_meta_command(db_bin):
    assert run_script(db_bin, [".exit"]) == [PROMPT]


def test_end_of_input_exits_cleanly(db_bin):
    assert run_script(db_bin, []) == [PROMPT]


def test_unrecognized_meta_command(db_bin):
    out = run_script(db_bin, [".foo", ".exit"])
    assert out[0] == PROMPT + "Unrecognized command '.foo'."


def test_unrecognized_keyword(db_bin):
    out = run_script(db_bin, ["hello", ".exit"])
    assert out[0] == PROMPT + "Unrecognized keyword at start of 'hello'."


def test_empty_lines_are_ignored(db_bin):
    assert run_script(db_bin, ["", "", ".exit"]) == [PROMPT * 3]
