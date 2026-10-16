import subprocess
import sys

from conftest import ROOT, TIMEOUT_SECONDS

BENCH = ROOT / "bench" / "bench.py"
WORKLOADS = 5


def test_bench_runs_every_workload(db_bin):
    # Tiny counts: this only checks that the script and its own checks still
    # work, not how fast anything is.
    proc = subprocess.run(
        [sys.executable, str(BENCH), "--binary", db_bin,
         "--rows", "60", "--lookups", "20", "--sync-rows", "5", "--cache-frames", "8"],
        capture_output=True, text=True, timeout=TIMEOUT_SECONDS,
    )
    assert proc.returncode == 0, proc.stderr
    rows = [line for line in proc.stdout.splitlines() if line.startswith("| ")]
    # A header row plus one row per workload.
    assert len(rows) == WORKLOADS + 1
