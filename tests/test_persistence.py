import subprocess

from conftest import PROMPT, TIMEOUT_SECONDS

PAGE_SIZE = 4096
ROW_SIZE = 293
ROWS_PER_PAGE = PAGE_SIZE // ROW_SIZE
TABLE_MAX_ROWS = ROWS_PER_PAGE * 100


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


def test_rows_spanning_several_pages_survive(repl):
    count = ROWS_PER_PAGE * 3 + 5
    repl([f"insert {i} u{i} e{i}" for i in range(count)] + [".exit"])
    assert select_rows(repl) == [f"({i}, u{i}, e{i})" for i in range(count)]


def test_file_length_encodes_the_row_count(repl, db_path):
    repl([f"insert {i} u{i} e{i}" for i in range(ROWS_PER_PAGE + 2)] + [".exit"])
    assert db_path.stat().st_size == PAGE_SIZE + 2 * ROW_SIZE


def test_full_table_stays_full_after_restart(repl):
    repl([f"insert {i} u{i} e{i}" for i in range(TABLE_MAX_ROWS)] + [".exit"])
    out = repl(["insert 9999 late l@x", ".exit"])
    assert out[0] == PROMPT + "Error: Table full."


def test_corrupt_file_length_is_rejected(db_bin, db_path):
    db_path.write_bytes(b"\0" * (ROW_SIZE + 1))
    proc = subprocess.run([db_bin, str(db_path)], input="", capture_output=True, text=True,
                          timeout=TIMEOUT_SECONDS)
    assert proc.returncode != 0
    assert "corrupt" in proc.stderr
