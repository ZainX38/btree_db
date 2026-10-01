from test_repl import PROMPT, run_script

USERNAME_MAX_LEN = 32
EMAIL_MAX_LEN = 255
# 4096 // 293 rows per page, 100 pages in the in-memory table.
TABLE_MAX_ROWS = 13 * 100


def test_insert_and_select(db_bin):
    out = run_script(db_bin, ["insert 1 alice alice@example.com", "select", ".exit"])
    assert out == [
        PROMPT + "Executed.",
        PROMPT + "(1, alice, alice@example.com)",
        "Executed.",
        PROMPT,
    ]


def test_select_returns_rows_in_insert_order(db_bin):
    out = run_script(db_bin, ["insert 2 bob b@x", "insert 1 amy a@x", "select", ".exit"])
    assert out[2:4] == [PROMPT + "(2, bob, b@x)", "(1, amy, a@x)"]


def test_max_length_strings_are_accepted(db_bin):
    username = "u" * USERNAME_MAX_LEN
    email = "e" * EMAIL_MAX_LEN
    out = run_script(db_bin, [f"insert 1 {username} {email}", "select", ".exit"])
    assert out[0] == PROMPT + "Executed."
    assert out[1] == PROMPT + f"(1, {username}, {email})"


def test_too_long_username_is_rejected(db_bin):
    out = run_script(db_bin, [f"insert 1 {'u' * (USERNAME_MAX_LEN + 1)} a@x", "select", ".exit"])
    assert out[0] == PROMPT + "String is too long."
    assert out[1] == PROMPT + "Executed."  # nothing was stored


def test_too_long_email_is_rejected(db_bin):
    out = run_script(db_bin, [f"insert 1 bob {'e' * (EMAIL_MAX_LEN + 1)}", ".exit"])
    assert out[0] == PROMPT + "String is too long."


def test_invalid_ids_are_rejected(db_bin):
    bad_ids = ["-1", "abc", "12abc", "4294967296"]
    out = run_script(db_bin, [f"insert {i} bob b@x" for i in bad_ids] + [".exit"])
    for line in out[: len(bad_ids)]:
        assert line == PROMPT + "ID must be an integer between 0 and 4294967295."


def test_largest_id_is_accepted(db_bin):
    out = run_script(db_bin, ["insert 4294967295 bob b@x", "select", ".exit"])
    assert out[1] == PROMPT + "(4294967295, bob, b@x)"


def test_wrong_number_of_fields_is_a_syntax_error(db_bin):
    out = run_script(db_bin, ["insert 1 bob", "insert 1 bob b@x extra", "select extra", ".exit"])
    assert out[:3] == [PROMPT + "Syntax error. Could not parse statement."] * 3


def test_table_full(db_bin):
    commands = [f"insert {i} user{i} user{i}@x" for i in range(TABLE_MAX_ROWS + 1)]
    out = run_script(db_bin, commands + [".exit"])
    assert out[TABLE_MAX_ROWS - 1] == PROMPT + "Executed."
    assert out[TABLE_MAX_ROWS] == PROMPT + "Error: Table full."
