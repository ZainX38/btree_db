"""Times btree_db on a few workloads by piping scripts into the REPL.

Each workload runs as one process, so the numbers include parsing, printing
and start-up: the same costs anyone using the REPL pays. Run it against the
release build (make release); the sanitizer build is several times slower.
"""

import argparse
import pathlib
import random
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_BIN = ROOT / "build" / "release" / "btree_db"
DEFAULT_ROWS = 50_000
DEFAULT_LOOKUPS = 10_000
# An fsync per insert costs about a millisecond, so the synced run uses far
# fewer rows than the others to finish in seconds.
DEFAULT_SYNC_ROWS = 1_000
# Fixed so that two runs (say, before and after a change) do the same work.
SEED = 42
SUCCESS = "Executed."
ROW_PREFIX = "("


def insert_commands(keys):
    return [f"insert {k} user{k} user{k}@example.com" for k in keys]


def lookup_commands(keys):
    return [f"select * from users where id = {k}" for k in keys]


def run(args, db_path, commands, sync=False):
    """Runs one session and returns (seconds, stdout lines)."""
    argv = [args.binary]
    if args.cache_frames is not None:
        argv += ["--cache-frames", str(args.cache_frames)]
    if not sync:
        argv.append("--no-sync")
    argv.append(str(db_path))
    script = "".join(c + "\n" for c in commands + [".exit"])
    start = time.perf_counter()
    proc = subprocess.run(argv, input=script, capture_output=True, text=True)
    seconds = time.perf_counter() - start
    if proc.returncode != 0 or proc.stderr:
        sys.exit(f"btree_db failed (exit {proc.returncode}):\n{proc.stderr}")
    return seconds, proc.stdout.split("\n")


def count_successes(lines):
    return sum(line.endswith(SUCCESS) for line in lines)


def count_rows(lines):
    # Row lines follow a "db > " prompt only when they are the first one printed.
    return sum(line.split("db > ")[-1].startswith(ROW_PREFIX) for line in lines)


def check(what, got, expected):
    # A benchmark that silently did less work than it claims is worse than none.
    if got != expected:
        sys.exit(f"{what}: expected {expected}, got {got}")


def bench_inserts(args, db_path, keys, sync=False):
    seconds, lines = run(args, db_path, insert_commands(keys), sync)
    check("inserts", count_successes(lines), len(keys))
    return seconds


def bench_lookups(args, db_path, keys):
    seconds, lines = run(args, db_path, lookup_commands(keys))
    check("lookups", count_rows(lines), len(keys))
    return seconds


def bench_scan(args, db_path, num_rows):
    seconds, lines = run(args, db_path, ["select"])
    check("full scan", count_rows(lines), num_rows)
    return seconds


def report(name, ops, seconds):
    print(f"| {name} | {ops:,} | {seconds:.3f} | {ops / seconds:,.0f} |")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--binary", default=str(DEFAULT_BIN))
    parser.add_argument("--rows", type=int, default=DEFAULT_ROWS)
    parser.add_argument("--lookups", type=int, default=DEFAULT_LOOKUPS)
    parser.add_argument("--sync-rows", type=int, default=DEFAULT_SYNC_ROWS)
    parser.add_argument("--cache-frames", type=int)
    args = parser.parse_args()
    if not pathlib.Path(args.binary).exists():
        sys.exit(f"binary not built: {args.binary} (run make release)")

    rng = random.Random(SEED)
    sequential = list(range(args.rows))
    shuffled = rng.sample(sequential, len(sequential))
    probes = [rng.randrange(args.rows) for _ in range(args.lookups)]

    print("| Workload | Operations | Seconds | Ops/sec |")
    print("|---|---:|---:|---:|")
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        report("Sequential insert", args.rows, bench_inserts(args, tmp / "seq.db", sequential))
        report("Random insert", args.rows, bench_inserts(args, tmp / "rand.db", shuffled))
        report("Point lookup", args.lookups, bench_lookups(args, tmp / "rand.db", probes))
        report("Full scan (rows)", args.rows, bench_scan(args, tmp / "rand.db", args.rows))
        synced = list(range(args.sync_rows))
        report("Insert with fsync", args.sync_rows,
               bench_inserts(args, tmp / "sync.db", synced, sync=True))


if __name__ == "__main__":
    main()
