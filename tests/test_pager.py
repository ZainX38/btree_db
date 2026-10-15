import os
import struct
import subprocess

import pytest

from conftest import TIMEOUT_SECONDS

PAGE_SIZE = 4096
# Leaf layout: node_type(1) is_root(1) parent(4) num_cells(4) next_leaf(4), then cells.
NODE_TYPE_OFFSET = 0
IS_ROOT_OFFSET = 1
NUM_CELLS_OFFSET = 6
LEAF_HEADER_SIZE = 14
NODE_LEAF = 1
# Each cell is key(4) then the 293-byte row (id, username, email).
CELL_KEY_OFFSET = LEAF_HEADER_SIZE
CELL_ROW_OFFSET = CELL_KEY_OFFSET + 4
ROW_USERNAME_OFFSET = 4


def test_database_file_is_created(repl, db_path):
    repl([".exit"])
    assert db_path.exists()


def test_file_is_always_whole_pages(repl, db_path):
    repl(["insert 7 alice a@x", ".exit"])
    assert db_path.stat().st_size == PAGE_SIZE


def test_page_zero_is_a_root_leaf(repl, db_path):
    repl(["insert 7 alice a@x", "insert 8 bob b@x", ".exit"])
    data = db_path.read_bytes()
    assert data[NODE_TYPE_OFFSET] == NODE_LEAF
    assert data[IS_ROOT_OFFSET] == 1
    # Integers are stored in the machine's native byte order (little-endian on x86).
    assert struct.unpack_from("<I", data, NUM_CELLS_OFFSET)[0] == 2


def test_first_cell_holds_key_then_row(repl, db_path):
    repl(["insert 7 alice a@x", ".exit"])
    data = db_path.read_bytes()
    assert struct.unpack_from("<I", data, CELL_KEY_OFFSET)[0] == 7
    assert struct.unpack_from("<I", data, CELL_ROW_OFFSET)[0] == 7
    username_at = CELL_ROW_OFFSET + ROW_USERNAME_OFFSET
    assert data[username_at : username_at + 6] == b"alice\0"


def test_partial_page_file_is_rejected(db_bin, db_path):
    db_path.write_bytes(b"\0" * (PAGE_SIZE + 1))
    proc = subprocess.run([db_bin, str(db_path)], input="", capture_output=True, text=True,
                          timeout=TIMEOUT_SECONDS)
    assert proc.returncode != 0
    assert "corrupt" in proc.stderr


def test_missing_filename_is_an_error(db_bin):
    proc = subprocess.run([db_bin], input="", capture_output=True, text=True,
                          timeout=TIMEOUT_SECONDS)
    assert proc.returncode != 0
    assert "Usage:" in proc.stderr


def test_read_only_session_writes_nothing(repl, db_path):
    # Only dirty pages are written back, so selecting and printing the tree
    # must leave the file untouched. Back-date the mtime first so even a
    # rewrite with identical bytes would be noticed.
    repl([f"insert {i} u{i} e{i}" for i in range(100)] + [".exit"])
    os.utime(db_path, ns=(1_000_000_000, 1_000_000_000))
    before = db_path.read_bytes()
    repl(["select", ".btree", ".validate", ".exit"])
    assert db_path.stat().st_mtime_ns == 1_000_000_000
    assert db_path.read_bytes() == before


@pytest.mark.parametrize(
    "options",
    [
        ["--no-sync"],
        ["--no-sync", "--cache-frames", "8"],
        ["--cache-frames", "8", "--no-sync"],
    ],
)
def test_options_can_come_in_any_order(db_bin, db_path, options):
    proc = subprocess.run([db_bin, *options, str(db_path)], input="insert 1 a b\nselect\n",
                          capture_output=True, text=True, timeout=TIMEOUT_SECONDS)
    assert proc.returncode == 0
    assert "(1, a, b)" in proc.stdout


@pytest.mark.parametrize(
    "args",
    [
        ["--sync", "DB"],
        ["--cache-frames", "DB"],
        ["--cache-frames", "8"],
        ["--no-sync", "--no-sync-please", "DB"],
        ["DB", "--no-sync"],
    ],
)
def test_bad_options_are_rejected(db_bin, db_path, args):
    args = [str(db_path) if arg == "DB" else arg for arg in args]
    proc = subprocess.run([db_bin, *args], input="", capture_output=True, text=True,
                          timeout=TIMEOUT_SECONDS)
    assert proc.returncode != 0
    assert "Usage:" in proc.stderr
