# btree_db

A small disk-based database written from scratch in C11. It stores rows in a B+tree of
4KB pages, keeps hot pages in an LRU cache, makes every statement durable through a
write-ahead log, and accepts a little SQL through a hand-written tokenizer and recursive
descent parser.

It is a learning project: the aim is to understand how a real storage engine works,
from bytes on disk up to a query prompt, so it favours clear code and thorough tests over
features. Only libc and POSIX are used, with no external libraries.

```
db > insert into users values (1, 'ada', 'ada@example.com');
Executed.
db > insert 2 alan alan@example.com
Executed.
db > select * from users where id >= 2;
(2, alan, alan@example.com)
Executed.
db > select * form users
Syntax error at column 10: expected FROM.
  select * form users
           ^
```

## Features

- **B+tree storage.** Rows live in leaf pages sorted by `id`, and leaves are linked so a
  scan walks them in order. Leaf splits, internal node splits and root splits let the tree
  grow to any depth, and lookups use binary search inside each node.
- **Pager with an LRU cache.** A fixed number of page frames (64 by default, set with
  `--cache-frames`), with pin counts, dirty tracking and least-recently-used eviction, so
  the database can be much larger than memory.
- **Write-ahead log and crash recovery.** After every statement, the changed pages and a
  commit record are appended to `<db>.wal` and `fsync`ed before `Executed.` is printed.
  On startup, committed changes are replayed and anything torn or uncommitted is dropped.
  Checkpoints copy the log into the database file and empty it.
- **A small SQL dialect.** `INSERT` and `SELECT` with `WHERE` on any column and the
  operators `= != < <= > >=`. A `WHERE` on `id` seeks straight to the first matching leaf
  instead of scanning the table. Syntax errors point at the exact column.
- **Debugging commands.** `.btree` prints the tree, `.validate` checks every structural
  invariant, and `.stats` shows cache and log counters.
- **Tested hard.** More than 150 pytest tests drive the real binary, built with
  AddressSanitizer and UndefinedBehaviorSanitizer. They include randomised inserts checked
  against a Python model, a build with a tiny fan-out to force deep trees, and crash tests
  that stop the process at every single disk write.

## Getting started

You need Linux (or WSL), gcc, GNU Make and Python 3.

```sh
make                      # debug build with ASan and UBSan: build/debug/btree_db
make release              # optimised build: build/release/btree_db

python3 -m venv .venv && .venv/bin/pip install pytest
make test                 # runs the test suite against both test builds

build/release/btree_db my.db
```

### Options

```
btree_db [--cache-frames N] [--no-sync] <database file>
```

| Option | Effect |
|---|---|
| `--cache-frames N` | Size of the page cache, from 4 to 1,048,576 frames of 4KB. Default 64. |
| `--no-sync` | Skip `fsync`. A killed process still loses nothing, but a power cut can lose recent statements. |

## Usage

The database has one table, `users`, with three columns:

| Column | Type |
|---|---|
| `id` | unsigned 32-bit integer, the primary key |
| `username` | up to 32 characters |
| `email` | up to 255 characters |

### Statements

```sql
insert into users values (7, 'grace', 'grace@example.com');
select * from users;
select * from users where id > 100;
select * from users where username = 'o''brien';
```

Keywords are case-insensitive, the trailing `;` is optional, and a quote inside a string
is written twice (`''`). The short forms `insert 7 grace grace@example.com` and `select`
also work.

<details>
<summary>Grammar</summary>

```
statement   = insert | select , [ ";" ] ;
insert      = "insert" , ( "into" , "users" , "values" , "(" , value , "," , value , "," , value , ")"
                         | value , value , value ) ;
select      = "select" , [ "*" , "from" , "users" ] , [ where ] ;
where       = "where" , column , operator , value ;
column      = "id" | "username" | "email" ;
operator    = "=" | "!=" | "<" | "<=" | ">" | ">=" ;
value       = word | "'" , { character | "''" } , "'" ;
(* a word is any unquoted run of characters, such as 42 or ada@example.com *)
```

</details>

### Meta commands

| Command | What it does |
|---|---|
| `.btree` | Prints the tree, one node per line, indented by depth. |
| `.validate` | Checks key order, parent pointers, leaf depth and the leaf chain. Prints `Tree is valid.` or the first problem found. |
| `.stats` | Cache frames, pages, hits, misses, evictions, peak pinned pages, WAL records and checkpoints. |
| `.exit` | Commits, checkpoints and exits. End of input does the same. |

With 14 rows, the first leaf has split and the root has become an internal node:

```
db > .btree
Tree:
- internal (page 0, size 1)
  - leaf (page 2, size 7)
    - 1
    ...
  - key 7
  - leaf (page 1, size 7)
    - 8
    ...
```

## Architecture

```
            REPL (main.c)         reads lines, runs meta commands, prints results
                 │
     tokenizer ─▶ parser          text ─▶ tokens ─▶ Statement
                 │
            executor              insert, or scan with an optional WHERE
                 │
       B+tree + cursor            node layout, search, splits, ordered iteration
                 │
              pager               page cache: pins, dirty pages, LRU eviction
              │     │
       <db>.wal     <db>          log of committed page images │ checkpointed pages
```

Each layer only talks to the one below it. The pager is the only code that touches the
database file, and the B+tree only ever sees 4KB buffers that it pins and unpins.

| File | Responsibility |
|---|---|
| `src/main.c` | REPL loop, command-line options, meta commands, error messages |
| `src/tokenizer.c` | Splits a line into tokens that point back into the input |
| `src/parser.c` | Recursive descent parser that builds a `Statement` |
| `src/executor.c` | Runs statements, including the seek for `WHERE id ...` |
| `src/btree.c` | Node accessors, binary search, leaf and internal splits, `.btree`, `.validate` |
| `src/cursor.c` | Walks the leaf chain in key order |
| `src/row.c` | Serialises a row into its fixed 293-byte on-disk form |
| `src/pager.c` | Page cache, eviction, commit, checkpoint and recovery |
| `src/wal.c` | WAL record format, append, replay |
| `src/crc32.c` | CRC-32 used to detect torn log records |
| `src/crashpoint.c` | Fault injection for crash tests (debug builds only) |

### Page layout

Every node is one 4KB page, and page 0 is always the root. Fields are read and written
through accessor functions at named offsets, never by casting the page to a struct.

```
Leaf node                               Internal node
offset  size  field                     offset  size  field
0       1     node type (1 = leaf)      0       1     node type (0 = internal)
1       1     is root                   1       1     is root
2       4     parent page               2       4     parent page
6       4     number of cells           6       4     number of keys
10      4     next leaf page            10      4     right child page
14      297   cell 0: key (4) + row     14      8     cell 0: child page (4) + key (4)
...                                     ...
```

A leaf holds 13 rows and an internal node up to 510 keys. Key *i* of an internal node is
the largest key in child *i*'s subtree, and anything larger lives under the right child.

### Write-ahead log

```
type (4) │ page number (4) │ CRC-32 (4) │ page image (4096, page records only)
```

- **Commit.** After each statement, every page it changed goes into the log, followed by
  a commit record, and then the log is `fsync`ed. Only then is the statement reported as
  done.
- **No steal.** Pages evicted from the cache in the middle of a statement go to the log
  as well, never to the database file. So the database file only ever holds committed
  pages. An index in memory remembers where each page's latest image is, and reads check
  it before the database file.
- **Checkpoint.** The latest image of every logged page is copied into the database file,
  which is `fsync`ed before the log is emptied. This happens on exit, after recovery and
  every 1,000 log records.
- **Recovery.** At startup the log is read until a record is cut short or fails its CRC.
  Only pages followed by a commit record are applied, so each statement survives a crash
  completely or not at all.

## Testing

Tests are written in Python with pytest and drive the compiled binary through pipes, the
same way a user would. `make test` runs them against two sanitised builds: the normal one,
and one where internal nodes hold only 3 keys, so a few hundred rows build a tree several
levels deep. Any sanitizer report fails the test.

| Area | What is checked |
|---|---|
| Rows and REPL | Serialisation, limits on ids and string lengths, error messages |
| B+tree | Splits at every boundary, deep trees, `.btree` output, `.validate` after every step |
| Random inserts | Thousands of shuffled keys compared with a sorted Python list |
| Cache | Eviction under an 8-frame cache, hit and miss counters, pin counts staying low |
| SQL | Tokens, every operator on every column, error columns and carets |
| WAL | Record format checked with Python's `zlib.crc32`, torn tails, flipped bits, uncommitted pages |
| Crashes | `SIGKILL` mid-stream, and a crash before each disk write of an insert that splits every level |

Crash tests set `BTREE_DB_CRASH_AFTER=N`, which makes the debug build exit just before its
*N*th disk write. After each crash the database is reopened and checked: every statement
that printed `Executed.` must be there, and nothing else.

## Benchmarks

```sh
make release && .venv/bin/python bench/bench.py
```

The script pipes generated statements into the release build and times each workload,
including process start-up, parsing and printing. It also checks that every statement
succeeded and every lookup found its row. Results with the default 64-frame cache and
`--no-sync`, except for the last row, on an Intel Core Ultra 7 258V under WSL2:

| Workload | Operations | Seconds | Ops/sec |
|---|---:|---:|---:|
| Sequential insert | 50,000 | 1.603 | 31,193 |
| Random insert | 50,000 | 1.274 | 39,237 |
| Point lookup (`where id = k`) | 10,000 | 0.041 | 242,185 |
| Full scan (rows) | 50,000 | 0.032 | 1,540,598 |
| Insert with fsync | 1,000 | 6.112 | 164 |

Inserts are bound by the log: every statement writes at least one full 4KB page image,
and with `fsync` turned on each one waits for the disk. Use `--rows`, `--lookups`,
`--sync-rows` and `--cache-frames` to try other sizes.

## Limitations

- One fixed table. No `CREATE TABLE`, `UPDATE`, `DELETE`, joins or secondary indexes.
- Pages are never freed or reused, since nothing is ever deleted.
- One process at a time: there is no file locking.
- Integers are stored in host byte order, so database files aren't portable between
  machines with different endianness.
- Each statement is its own transaction; there is no `BEGIN`/`COMMIT`.

## Further reading

- [`docs/LEARNING.md`](docs/LEARNING.md) has notes for each stage of the build: what was
  built, the C concepts behind it, the hardest part and practice interview questions.
- [`docs/TIMELINE.md`](docs/TIMELINE.md) has the build plan, the hour estimates and what
  the commit dates mean.

This project draws on these resources:

- [cstack/db_tutorial](https://github.com/cstack/db_tutorial), the "Let's Build a Simple
  Database" tutorial, which the REPL, row format and page layouts start from.
- [CMU 15-445/645](https://15445.courses.cs.cmu.edu/) and its
  [BusTub](https://github.com/cmu-db/bustub) system, for the buffer pool and recovery.
- [toydb](https://github.com/erikgrinaker/toydb), a distributed SQL database in Rust
  built for learning.
- [SQLite's write-ahead logging](https://www.sqlite.org/wal.html), the model for the WAL
  and checkpoints.
