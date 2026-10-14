from conftest import PROMPT, select_rows

MAX_TOKENS = 32


def test_keywords_are_case_insensitive(repl):
    out = repl(["INSERT 1 amy a@x", "Insert 2 bob b@x", "SeLeCt", ".exit"])
    assert out[:4] == [
        PROMPT + "Executed.",
        PROMPT + "Executed.",
        PROMPT + "(1, amy, a@x)",
        "(2, bob, b@x)",
    ]


def test_quoted_strings_can_hold_spaces(repl):
    repl(["insert 1 'amy lee' 'amy lee@x'", ".exit"])
    assert select_rows(repl) == ["(1, amy lee, amy lee@x)"]


def test_doubled_quote_is_an_escaped_quote(repl):
    repl(["insert 1 'o''brien' 'it''s@x'", ".exit"])
    assert select_rows(repl) == ["(1, o'brien, it's@x)"]


def test_empty_quoted_string_is_a_value(repl):
    repl(["insert 1 '' ''", ".exit"])
    assert select_rows(repl) == ["(1, , )"]


def test_quoted_keyword_is_just_a_string(repl):
    repl(["insert 1 'select' 'insert'", ".exit"])
    assert select_rows(repl) == ["(1, select, insert)"]


def test_extra_whitespace_and_tabs_are_ignored(repl):
    out = repl(["  insert\t1   amy\ta@x  ", "\tselect  ", ".exit"])
    assert out[:2] == [PROMPT + "Executed.", PROMPT + "(1, amy, a@x)"]


def test_unterminated_string_is_a_syntax_error(repl):
    out = repl(["insert 1 'amy a@x", "select", ".exit"])
    assert out[0] == PROMPT + "Syntax error at column 10: string is never closed."
    assert out[3] == PROMPT + "Executed."


def test_too_many_tokens_is_a_syntax_error(repl):
    out = repl(["select" + " x" * MAX_TOKENS, ".exit"])
    # Token 33 is the last "x", at column 6 + 2 * 31 + 2.
    assert out[0] == PROMPT + "Syntax error at column 70: too many tokens."


def test_escaped_quotes_count_once_towards_the_length_limit(repl):
    # 32 characters once each '' becomes ', which is exactly the limit.
    username = "''" * 32
    out = repl([f"insert 1 '{username}' a@x", ".exit"])
    assert out[0] == PROMPT + "Executed."
