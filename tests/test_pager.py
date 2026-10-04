import struct
import subprocess

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
