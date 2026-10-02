import struct
import subprocess

from conftest import TIMEOUT_SECONDS

PAGE_SIZE = 4096
ROW_SIZE = 293
ROWS_PER_PAGE = PAGE_SIZE // ROW_SIZE
USERNAME_OFFSET = 4


def test_database_file_is_created(repl, db_path):
    repl([".exit"])
    assert db_path.exists()


def test_rows_are_written_into_page_zero(repl, db_path):
    repl(["insert 7 alice a@x", ".exit"])
    data = db_path.read_bytes()
    # The id is stored in the machine's native byte order (little-endian on x86).
    assert struct.unpack_from("<I", data, 0)[0] == 7
    assert data[USERNAME_OFFSET : USERNAME_OFFSET + 6] == b"alice\0"


def test_second_page_starts_at_4096(repl, db_path):
    first_id_on_page_1 = ROWS_PER_PAGE
    repl([f"insert {i} u{i} e{i}" for i in range(ROWS_PER_PAGE + 1)] + [".exit"])
    data = db_path.read_bytes()
    assert struct.unpack_from("<I", data, PAGE_SIZE)[0] == first_id_on_page_1


def test_missing_filename_is_an_error(db_bin):
    proc = subprocess.run([db_bin], input="", capture_output=True, text=True,
                          timeout=TIMEOUT_SECONDS)
    assert proc.returncode != 0
    assert "Usage:" in proc.stderr
