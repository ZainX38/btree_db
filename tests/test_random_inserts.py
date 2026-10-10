"""Thousands of random inserts, checked against a sorted Python reference.

Keys are drawn with replacement, so some repeat: the reference also predicts
exactly which inserts must be rejected as duplicates.
"""

import random

import pytest

from conftest import PROMPT, select_rows

UINT32_MAX = 2**32 - 1


def insert(i):
    return f"insert {i} u{i} e{i}"


def row(i):
    return f"({i}, u{i}, e{i})"


def run_and_check(repl, keys, reference):
    """Insert `keys` in one session; check every reply against `reference`."""
    out = repl([insert(k) for k in keys] + [".validate", ".exit"])
    for line, key in zip(out, keys):
        if key in reference:
            assert line == PROMPT + f"Error: Duplicate key {key}."
        else:
            assert line == PROMPT + "Executed."
            reference.add(key)
    assert out[len(keys)] == PROMPT + "Tree is valid."


@pytest.mark.parametrize("seed", [1, 2, 3])
def test_random_inserts_default_fan_out(repl, seed):
    rng = random.Random(seed)
    keys = [rng.randrange(20_000) for _ in range(3000)]
    reference = set()
    run_and_check(repl, keys, reference)
    assert select_rows(repl) == [row(k) for k in sorted(reference)]


@pytest.mark.parametrize("seed", [4, 5, 6])
def test_random_inserts_small_fan_out(small_fanout_repl, seed):
    rng = random.Random(seed)
    keys = [rng.randrange(5_000) for _ in range(2000)]
    reference = set()
    run_and_check(small_fanout_repl, keys, reference)
    assert select_rows(small_fanout_repl) == [row(k) for k in sorted(reference)]


def test_random_keys_across_the_full_range(small_fanout_repl):
    rng = random.Random(8)
    keys = [rng.randrange(UINT32_MAX + 1) for _ in range(1000)] + [0, UINT32_MAX]
    reference = set()
    run_and_check(small_fanout_repl, keys, reference)
    assert select_rows(small_fanout_repl) == [row(k) for k in sorted(reference)]


def test_random_inserts_across_restarts(small_fanout_repl):
    rng = random.Random(9)
    reference = set()
    for _ in range(4):  # each batch is a separate process on the same file
        run_and_check(small_fanout_repl, [rng.randrange(3_000) for _ in range(400)], reference)
    assert select_rows(small_fanout_repl) == [row(k) for k in sorted(reference)]
