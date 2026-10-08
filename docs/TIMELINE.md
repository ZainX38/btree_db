# Timeline

> **Commit dates in this repo are simulated estimates of how long I'd take to build this
> myself. They are not a real development history.** The code was written as a reference
> build with an AI assistant (every commit carries a `Co-Authored-By: Claude` trailer).
> When I build the project myself, I'll start a fresh repo with real dates.

## Assumptions behind the estimate

- Second-year SE student, comfortable in Python, JS/TS and Java, new to C.
- Sessions of 2-3 hours on most days, usually late evening into the early hours,
  around lectures and a delivery job.
- Hours include reading docs, debugging and fighting sanitizer reports.
  The B+tree splits, internal nodes and the WAL get the most time.

## The schedule is compressed

The estimate adds up to **about 95 hours**. At my real pace (about 12 hours a week) that
is **about 8 weeks** of work. The commit dates only cover **2026-09-30 to 2026-10-17
(18 days)**, because the repo's initial commit is dated 2026-09-29 and dates must move
forward. So the dates squeeze the work roughly **3x** compared with how fast I'd really go.
Use the **Hours** column, not the gaps between dates, as the estimate.

## Hours per phase

| Phase | Work | Hours |
|---|---|---|
| 1 | Makefile, REPL, row serialization | 6 |
| 2 | Simple pager, persistence, pytest harness | 7 |
| 3 | Single-leaf B+tree and cursor | 8 |
| 4 | Leaf splits and root splits | 12 |
| 5 | Internal nodes and multi-level trees | 16 |
| 6 | LRU cache, dirty tracking, pin counts, eviction test | 10 |
| 7 | Tokenizer and recursive descent parser | 10 |
| 8 | WAL, recovery and crash test | 18 |
| 9 | Benchmarks, README, cleanup | 8 |
| | **Total** | **95** |

## Changes to the plan while building

Hours per phase are unchanged; only what goes into some commits moved.

- **Commit 3** stores rows in a temporary in-memory table with a simple parser, so row
  serialization could be tested before the pager and SQL parser existed.
- **Commits 11-13.** A root split needs an internal node, and inserts and selects must be
  able to go through it, or every commit after the split would fail its tests. So the
  internal node layout moved into phase 4, the leaf split and root split became one commit,
  and linking leaves through `next_leaf` happened there too.
- **Commit 13** was planned as a fix for an off-by-one in the split. A bug can't honestly
  be planted to be fixed later, so this commit became the boundary tests that would catch
  that off-by-one.
- **Phase 5** was rewritten to match: commits 14-17 now cover inserting into the parent,
  a small fan-out test build, internal splits and tree invariant checks.

## Commit schedule (all times +01:00)

| # | Date / time | Phase | Hours | Message |
|---|---|---|---|---|
| 1 | Wed 2026-09-30 23:41 | 1 | 1.5 | Set up Makefile with sanitizer and release builds and added CLAUDE.md |
| 2 | Thu 2026-10-01 01:18 | 1 | 2 | feat: REPL loop that reads commands and handles .exit |
| 3 | Thu 2026-10-01 02:07 | 1 | 2.5 | feat: insert and select rows serialized into 293 byte slots in memory |
| 4 | Fri 2026-10-02 18:12 | 2 | 2 | Added pytest helpers to run scripted sessions against the REPL |
| 5 | Sat 2026-10-03 00:47 | 2 | 3 | feat: simple pager that reads and writes 4KB pages from the db file |
| 6 | Sat 2026-10-03 01:39 | 2 | 2 | feat: rows persist after restart, with a persistence test |
| 7 | Mon 2026-10-05 00:33 | 3 | 2.5 | feat: leaf node layout with header and cell accessors |
| 8 | Mon 2026-10-05 02:21 | 3 | 2.5 | feat: cursor to walk leaf cells, select now uses it |
| 9 | Mon 2026-10-05 23:58 | 3 | 3 | feat: binary search in leaf and reject duplicate keys |
| 10 | Wed 2026-10-07 00:52 | 4 | 1.5 | Added .btree meta command to print the tree for debugging |
| 11 | Wed 2026-10-07 02:44 | 4 | 3 | feat: internal node layout with key and child accessors |
| 12 | Wed 2026-10-07 18:31 | 4 | 5 | feat: split a full root leaf and rewrite page 0 as an internal root |
| 13 | Thu 2026-10-08 01:56 | 4 | 2.5 | Added split boundary tests for keys landing in either half |
| 14 | Thu 2026-10-08 03:07 | 5 | 3 | feat: insert a split leaf into its parent internal node |
| 15 | Fri 2026-10-09 17:49 | 5 | 2.5 | Added a small fan-out test build so tests can reach deep trees |
| 16 | Sat 2026-10-10 01:28 | 5 | 4 | feat: split internal nodes so the tree can grow past two levels |
| 17 | Sat 2026-10-10 14:36 | 5 | 4.5 | Added .btree invariant checks for key order, parent pointers and the leaf chain |
| 18 | Sat 2026-10-10 16:12 | 5 | 2 | Added random insert test checked against a sorted Python list |
| 19 | Mon 2026-10-12 00:41 | 6 | 3.5 | feat: page cache with fixed frames and dirty page tracking |
| 20 | Mon 2026-10-12 02:33 | 6 | 4 | feat: LRU eviction with pin counts and configurable cache size |
| 21 | Mon 2026-10-12 18:24 | 6 | 2.5 | Added beyond memory test that checks the eviction counter |
| 22 | Tue 2026-10-13 18:47 | 7 | 3.5 | feat: tokenizer with case-insensitive keywords and quoted strings |
| 23 | Wed 2026-10-14 00:29 | 7 | 4.5 | feat: recursive descent parser for insert and select with where |
| 24 | Wed 2026-10-14 01:52 | 7 | 2 | Parse errors now show the column where the problem is |
| 25 | Wed 2026-10-14 23:46 | 8 | 4 | feat: crc32 and WAL record format with append and fsync |
| 26 | Thu 2026-10-15 02:13 | 8 | 5 | feat: replay WAL on startup until a bad CRC or truncated record |
| 27 | Thu 2026-10-15 18:38 | 8 | 3 | feat: checkpoint on clean exit and every N records |
| 28 | Fri 2026-10-16 01:44 | 8 | 6 | Added crash test that kills the REPL mid-write and checks acknowledged rows |
| 29 | Fri 2026-10-16 02:58 | 9 | 2 | perf: benchmark script for sequential and random inserts and lookups |
| 30 | Sat 2026-10-17 13:26 | 9 | 4 | docs: README with architecture, page layouts, WAL and benchmarks |
| 31 | Sat 2026-10-17 15:14 | 9 | 2 | docs: final timeline totals and cleanup |
