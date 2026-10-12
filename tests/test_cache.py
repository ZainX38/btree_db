"""The LRU page cache: correctness when the cache is far smaller than the data."""

import random
import subprocess

import pytest

from conftest import PROMPT, SMALL_FANOUT_BIN, TIMEOUT_SECONDS, make_runner, select_rows

PAGER_MIN_FRAMES = 4


def insert(i):
    return f"insert {i} u{i} e{i}"


def row(i):
    return f"({i}, u{i}, e{i})"


def check_random_inserts(repl, seed, count, key_range):
    rng = random.Random(seed)
    keys = [rng.randrange(key_range) for _ in range(count)]
    out = repl([insert(k) for k in keys] + [".validate", ".exit"])
    seen = set()
    for line, key in zip(out, keys):
        expected = f"Error: Duplicate key {key}." if key in seen else "Executed."
        assert line == PROMPT + expected
        seen.add(key)
    assert out[len(keys)] == PROMPT + "Tree is valid."
    assert select_rows(repl) == [row(k) for k in sorted(seen)]


@pytest.mark.parametrize("bad", ["3", "0", "abc", "-1", "8x", ""])
def test_bad_cache_sizes_are_rejected(db_bin, db_path, bad):
    proc = subprocess.run([db_bin, "--cache-frames", bad, str(db_path)], input="",
                          capture_output=True, text=True, timeout=TIMEOUT_SECONDS)
    assert proc.returncode != 0
    assert proc.stderr != ""


def test_random_inserts_with_a_tiny_cache(tiny_cache_repl):
    check_random_inserts(tiny_cache_repl, seed=21, count=2000, key_range=10_000)


def test_deep_tree_with_a_tiny_cache(tiny_cache_small_fanout_repl):
    check_random_inserts(tiny_cache_small_fanout_repl, seed=22, count=1500, key_range=5_000)


def test_smallest_allowed_cache(db_path):
    # Splits pin at most two pages at once, so even the minimum cache must cope
    # with a deep tree and its internal splits.
    repl = make_runner(str(SMALL_FANOUT_BIN), db_path, PAGER_MIN_FRAMES)
    check_random_inserts(repl, seed=23, count=1000, key_range=3_000)


def test_tiny_cache_data_survives_restarts(tiny_cache_small_fanout_repl):
    rng = random.Random(24)
    keys = rng.sample(range(50_000), 900)
    for start in range(0, len(keys), 300):
        tiny_cache_small_fanout_repl([insert(k) for k in keys[start : start + 300]] + [".exit"])
    assert select_rows(tiny_cache_small_fanout_repl) == [row(k) for k in sorted(keys)]
