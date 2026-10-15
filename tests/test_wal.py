"""Reads the WAL file directly and checks its records against zlib's CRC-32.

A clean exit checkpoints the log into the database file and empties it, so
these tests look at the log of a session that was killed instead.
"""

import struct
import zlib

import pytest

from conftest import PAGE_SIZE_BYTES, TINY_CACHE_FRAMES, make_killer

HEADER = struct.Struct("<III")  # type, page_num, crc32
RECORD_PAGE = 1
RECORD_COMMIT = 2


def insert(i):
    return f"insert {i} u{i} e{i}"


def wal_path(db_path):
    return db_path.with_name(db_path.name + ".wal")


def record_spans(db_path):
    """Return [(type, page_num, start, end)], asserting every record is intact."""
    data = wal_path(db_path).read_bytes()
    spans, pos = [], 0
    while pos < len(data):
        kind, page_num, crc = HEADER.unpack_from(data, pos)
        size = HEADER.size + (PAGE_SIZE_BYTES if kind == RECORD_PAGE else 0)
        payload = data[pos + HEADER.size : pos + size]
        assert kind in (RECORD_PAGE, RECORD_COMMIT)
        assert zlib.crc32(payload, zlib.crc32(struct.pack("<II", kind, page_num))) == crc
        spans.append((kind, page_num, pos, pos + size))
        pos += size
    return spans


def read_wal(db_path):
    """Return [(type, page_num, payload)], asserting every record is intact."""
    data = wal_path(db_path).read_bytes()
    return [
        (kind, page_num, data[start + HEADER.size : end])
        for kind, page_num, start, end in record_spans(db_path)
    ]


def commits(records):
    return sum(1 for kind, _, _ in records if kind == RECORD_COMMIT)


@pytest.fixture
def killed_repl(db_bin, db_path):
    return make_killer(db_bin, db_path)


def test_new_database_logs_its_empty_root(killed_repl, db_path):
    killed_repl([])
    records = read_wal(db_path)
    assert [(kind, page) for kind, page, _ in records] == [(RECORD_PAGE, 0), (RECORD_COMMIT, 0)]


def test_each_insert_logs_its_pages_then_a_commit(killed_repl, db_path):
    killed_repl([insert(1), insert(2)])
    records = read_wal(db_path)
    assert commits(records) == 3  # the empty root, then one per insert
    assert records[-1][0] == RECORD_COMMIT


def test_reads_and_failed_inserts_log_nothing(killed_repl, db_path):
    killed_repl([insert(1), "select", insert(1), ".btree"])
    assert commits(read_wal(db_path)) == 2


def test_database_file_only_changes_at_a_checkpoint(killed_repl, repl, db_path):
    repl([insert(i) for i in range(30)] + [".exit"])
    assert wal_path(db_path).stat().st_size == 0
    before = db_path.read_bytes()
    killed_repl([insert(i) for i in range(30, 60)])
    assert db_path.read_bytes() == before
    assert commits(read_wal(db_path)) == 30


def test_pages_evicted_mid_statement_are_logged(db_bin, db_path):
    killed_tiny_cache_repl = make_killer(db_bin, db_path, TINY_CACHE_FRAMES)
    out = killed_tiny_cache_repl([insert(i) for i in range(400)] + [".stats"])
    pages = next(int(line.split(": ")[1]) for line in out if "pages: " in line)
    records = read_wal(db_path)
    assert commits(records) == 401
    assert {page for kind, page, _ in records if kind == RECORD_PAGE} == set(range(pages))
    # Nothing was checkpointed, so the database file is still empty.
    assert db_path.stat().st_size == 0


def test_clean_exit_checkpoints_and_empties_the_log(repl, db_path):
    repl([insert(i) for i in range(100)] + [".exit"])
    assert wal_path(db_path).stat().st_size == 0
    assert db_path.stat().st_size > PAGE_SIZE_BYTES
