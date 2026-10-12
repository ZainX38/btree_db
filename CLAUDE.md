# btree_db

A B+tree database in C11 with a pager, LRU page cache, write-ahead log and a small SQL parser.
This repo is a reference build; see `docs/TIMELINE.md` for what the commit dates mean.

## Commands

Everything builds and runs on Linux. On this Windows machine, run commands inside WSL
(`wsl make`, `wsl make test`, ...).

| Task | Command |
|---|---|
| Debug build (ASan + UBSan, the default) | `make` |
| Release build (`-O2`, no sanitizers) | `make release` |
| Tests (pytest, against the debug build and a small fan-out build) | `make test` |
| Benchmarks (release build) | `make release && .venv/bin/python bench/bench.py` |
| One-time test setup | `python3 -m venv .venv && .venv/bin/pip install pytest` |
| Run with a smaller cache | `build/debug/btree_db --cache-frames 8 test.db` |

## Rules

- C11, Linux, gcc, GNU Make. Only libc and POSIX, no external libraries.
- Every commit must compile with `-Wall -Wextra -Werror` and pass `make test`.
- Comments explain *why*, not *what*.
- Small functions. Every magic number gets a named constant.
- On-disk fields are read and written through accessor functions at named offsets,
  never by casting page bytes to a struct.
- After each phase, add a section to `docs/LEARNING.md`.
- The repo is local only: never add a remote or push.

## Commit style

- One logical change per commit, usually 2-6 files and under ~300 added lines.
- Subject line only, no body (apart from the Co-Authored-By trailer).
- Either a conventional prefix in lowercase with an imperative verb
  (`feat: split a full leaf into two`, `docs: ...`, `perf: ...`, `fix: ...`),
  or a plain past-tense sentence in sentence case
  (`Added crash test that kills the REPL mid-write`).
- British spelling (initialised, organised).
- Commit dates follow the simulated schedule in `docs/TIMELINE.md`; set both
  `GIT_AUTHOR_DATE` and `GIT_COMMITTER_DATE`.
