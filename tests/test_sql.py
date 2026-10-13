"""The SQL forms of insert and select, checked against the short REPL forms and Python."""

import operator
import random

import pytest

from conftest import PROMPT

SYNTAX_ERROR = "Syntax error. Could not parse statement."
INVALID_ID = "ID must be an integer between 0 and 4294967295."
OPERATORS = {
    "=": operator.eq,
    "!=": operator.ne,
    "<": operator.lt,
    "<=": operator.le,
    ">": operator.gt,
    ">=": operator.ge,
}


def insert(i):
    return f"insert {i} u{i} e{i}"


def row(i):
    return f"({i}, u{i}, e{i})"


def query(repl, setup, sql):
    """Run `setup`, then `sql`, and return only the rows `sql` printed."""
    out = repl(list(setup) + [sql, ".exit"])
    lines = out[len(setup) :]
    lines[0] = lines[0].removeprefix(PROMPT)
    end = lines.index("Executed.")
    return lines[:end]


def test_sql_insert_and_select(repl):
    out = repl(
        [
            "insert into users values (1, 'amy lee', 'a@x')",
            "INSERT INTO Users VALUES(2,bob,b@x);",
            "select * from users",
            ".exit",
        ]
    )
    assert out[:4] == [
        PROMPT + "Executed.",
        PROMPT + "Executed.",
        PROMPT + "(1, amy lee, a@x)",
        "(2, bob, b@x)",
    ]


def test_trailing_semicolon_is_optional(repl):
    assert query(repl, [insert(1)], "select * from users;") == [row(1)]
    assert query(repl, [], "select;") == [row(1)]


def test_where_on_id(repl):
    setup = [insert(i) for i in range(1, 6)]
    assert query(repl, setup, "select * from users where id = 3") == [row(3)]
    assert query(repl, [], "select where id >= 4") == [row(4), row(5)]
    assert query(repl, [], "select * from users where id = 99") == []


def test_where_on_strings(repl):
    setup = [insert(1), "insert 2 'amy lee' a@x", "insert 3 bob b@x"]
    assert query(repl, setup, "select * from users where username = 'amy lee'") == [
        "(2, amy lee, a@x)"
    ]
    assert query(repl, [], "select where email < b") == ["(2, amy lee, a@x)"]
    assert query(repl, [], "select where USERNAME != u1") == ["(2, amy lee, a@x)", "(3, bob, b@x)"]


def test_where_on_largest_and_smallest_id(repl):
    setup = ["insert 0 a a@x", "insert 4294967295 z z@x"]
    assert query(repl, setup, "select where id > 0") == ["(4294967295, z, z@x)"]
    assert query(repl, [], "select where id < 4294967295") == ["(0, a, a@x)"]
    assert query(repl, [], "select where id > 4294967295") == []


@pytest.mark.parametrize("op", OPERATORS)
def test_where_on_id_matches_python_on_a_deep_tree(small_fanout_repl, op):
    rng = random.Random(70 + list(OPERATORS).index(op))
    keys = rng.sample(range(2000), 400)
    small_fanout_repl([insert(k) for k in keys] + [".exit"])
    bounds = [0, min(keys), max(keys), max(keys) + 1] + rng.sample(range(2000), 6)
    for bound in bounds:
        expected = [row(k) for k in sorted(keys) if OPERATORS[op](k, bound)]
        assert query(small_fanout_repl, [], f"select * from users where id {op} {bound}") == expected


@pytest.mark.parametrize(
    "sql",
    [
        "select *",
        "select * from",
        "select * from orders",
        "select from users",
        "select * from users where",
        "select * from users where id",
        "select * from users where id ==",
        "select * from users where name = 1",
        "select * from users where id = 1 and",
        "insert into users values (1, a, b",
        "insert into users values 1, a, b)",
        "insert into users (1, a, b)",
        "insert into users values (1 a b)",
        "insert into users values (1, a)",
        "insert 1 a b;;",
    ],
)
def test_malformed_sql_is_a_syntax_error(repl, sql):
    assert repl([sql, ".exit"])[0] == PROMPT + SYNTAX_ERROR


def test_bad_id_in_where_or_values(repl):
    out = repl(["select where id = abc", "insert into users values (-1, a, b)", ".exit"])
    assert out[:2] == [PROMPT + INVALID_ID] * 2


def test_too_long_values_are_rejected(repl):
    out = repl(
        [
            f"insert into users values (1, '{'u' * 33}', a)",
            f"select where username = '{'u' * 33}'",
            ".exit",
        ]
    )
    assert out[:2] == [PROMPT + "String is too long."] * 2
