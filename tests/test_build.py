import subprocess


def test_binary_runs_cleanly(db_bin):
    # A sanitizer report would show up on stderr even if the exit code were 0.
    proc = subprocess.run([db_bin], input="", capture_output=True, text=True, timeout=30)
    assert proc.returncode == 0
    assert proc.stderr == ""
