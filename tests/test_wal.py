"""Reads the WAL file directly and checks its records against zlib's CRC-32."""

import struct
import zlib

from conftest import PAGE_SIZE_BYTES, TINY_CACHE_FRAMES, make_runner

HEADER = struct.Struct("<III")  # type, page_num, crc32
RECORD_PAGE = 1
RECORD_COMMIT = 2


def insert(i):
    return f"insert {i} u{i} e{i}"


def wal_path(db_path):
    return db_path.with_name(db_path.name + ".wal")


def read_wal(db_path):
    """Return [(type, page_num, payload)], asserting every record is intact."""
    data = wal_path(db_path).read_bytes()
    records, pos = [], 0
    while pos < len(data):
        kind, page_num, crc = HEADER.unpack_from(data, pos)
        pos += HEADER.size
        size = PAGE_SIZE_BYTES if kind == RECORD_PAGE else 0
        payload = data[pos : pos + size]
        pos += size
        assert kind in (RECORD_PAGE, RECORD_COMMIT)
        assert zlib.crc32(payload, zlib.crc32(struct.pack("<II", kind, page_num))) == crc
        records.append((kind, page_num, payload))
    return records


def commits(records):
    return sum(1 for kind, _, _ in records if kind == RECORD_COMMIT)


def test_new_database_logs_its_empty_root(repl, db_path):
    repl([".exit"])
    records = read_wal(db_path)
    assert [(kind, page) for kind, page, _ in records] == [(RECORD_PAGE, 0), (RECORD_COMMIT, 0)]


def test_each_insert_logs_its_pages_then_a_commit(repl, db_path):
    repl([insert(1), insert(2), ".exit"])
    records = read_wal(db_path)
    assert commits(records) == 3  # the empty root, then one per insert
    assert records[-1][0] == RECORD_COMMIT


def test_latest_logged_image_matches_the_database_file(repl, db_path):
    repl([insert(i) for i in range(30)] + [".exit"])
    latest = {page: payload for kind, page, payload in read_wal(db_path) if kind == RECORD_PAGE}
    data = db_path.read_bytes()
    for page, payload in latest.items():
        assert data[page * PAGE_SIZE_BYTES : (page + 1) * PAGE_SIZE_BYTES] == payload


def test_reads_and_failed_inserts_log_nothing(repl, db_path):
    repl([insert(1), ".exit"])
    size = wal_path(db_path).stat().st_size
    repl(["select", insert(1), ".btree", ".validate", ".exit"])
    assert wal_path(db_path).stat().st_size == size


def test_pages_evicted_mid_statement_are_logged(db_bin, db_path):
    run = make_runner(db_bin, db_path, TINY_CACHE_FRAMES)
    run([insert(i) for i in range(400)] + [".exit"])
    records = read_wal(db_path)
    assert commits(records) == 401
    logged_pages = {page for kind, page, _ in records if kind == RECORD_PAGE}
    assert logged_pages == set(range(len(db_path.read_bytes()) // PAGE_SIZE_BYTES))
