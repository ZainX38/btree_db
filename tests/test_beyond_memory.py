"""Data sets far bigger than the page cache.

With 8 frames (32KB of cache) and hundreds of pages on disk, almost every
page access has to evict something. These tests read the pager's counters
through .stats to prove eviction really happened, and check the data against
a Python reference to prove nothing was lost along the way.
"""

import random

from conftest import PAGE_SIZE_BYTES, PROMPT, TINY_CACHE_FRAMES, select_rows

# The file must be at least this many times bigger than the cache.
MIN_PAGES_PER_FRAME = 10
# The most pages any operation should pin at once (see PAGER_MIN_FRAMES).
MAX_EXPECTED_PINS = 2


def insert(i):
    return f"insert {i} u{i} e{i}"


def row(i):
    return f"({i}, u{i}, e{i})"


def stats(out):
    """Parse the "name: value" lines printed by .stats."""
    values = {}
    for line in out:
        name, sep, value = line.removeprefix(PROMPT).partition(": ")
        if sep and value.isdigit():
            values[name] = int(value)
    return values


def test_inserts_far_beyond_the_cache(tiny_cache_repl, db_path):
    keys = random.Random(31).sample(range(1_000_000), 4000)
    out = tiny_cache_repl([insert(k) for k in keys] + [".stats", ".validate", ".exit"])
    counters = stats(out)

    assert counters["frames"] == TINY_CACHE_FRAMES
    assert counters["pages"] >= MIN_PAGES_PER_FRAME * TINY_CACHE_FRAMES
    assert counters["evictions"] > 0
    assert counters["peak pinned"] <= MAX_EXPECTED_PINS
    assert PROMPT + "Tree is valid." in out
    assert db_path.stat().st_size == counters["pages"] * PAGE_SIZE_BYTES
    assert select_rows(tiny_cache_repl) == [row(k) for k in sorted(keys)]


def test_full_scan_streams_through_the_cache(tiny_cache_repl):
    keys = list(range(3000))
    tiny_cache_repl([insert(k) for k in keys] + [".exit"])
    # A fresh session only reads, so every eviction here comes from the scan.
    out = tiny_cache_repl(["select", ".stats", ".exit"])
    rows = [line.removeprefix(PROMPT) for line in out[: len(keys)]]
    assert rows == [row(k) for k in keys]
    counters = stats(out)
    assert counters["evictions"] >= counters["pages"] - TINY_CACHE_FRAMES


def test_deep_tree_beyond_the_cache(tiny_cache_small_fanout_repl):
    keys = random.Random(32).sample(range(1_000_000), 2500)
    out = tiny_cache_small_fanout_repl([insert(k) for k in keys] + [".stats", ".exit"])
    counters = stats(out)
    assert counters["evictions"] > 0
    assert counters["peak pinned"] <= MAX_EXPECTED_PINS
    assert select_rows(tiny_cache_small_fanout_repl) == [row(k) for k in sorted(keys)]


def test_no_evictions_when_everything_fits(repl):
    out = repl([insert(k) for k in range(100)] + [".stats", ".exit"])
    counters = stats(out)
    assert counters["pages"] < counters["frames"]
    assert counters["evictions"] == 0
