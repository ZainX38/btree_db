# Learning notes

One section per phase: what was built, the C concepts behind it, the trickiest part,
and interview questions to practise with.

## Phase 1: Makefile, REPL, row serialization

### What was built

- A Makefile with a sanitized debug build (the default, used by tests) and an `-O2`
  release build, each with its own object directory and automatic header dependencies.
- A REPL that reads lines with `getline()`, handles `.exit`, and treats end of input like
  `.exit`.
- A `Row` type (id, username, email) that serializes to exactly 293 bytes, plus a
  temporary whitespace parser and an in-memory table of 4KB pages to store rows in.
- A pytest suite that drives the binary through a subprocess and fails on any stderr
  output, so sanitizer reports fail tests.

### Key C concepts

- **Struct padding.** `sizeof(Row)` is 296, not 293, because the compiler pads the struct
  for alignment. That's why rows are copied field by field with `memcpy` at named offsets
  instead of copying the whole struct onto disk.
- **Strings are byte arrays plus a NUL.** A 32-character username needs 33 bytes. Data
  read back from storage might not contain a NUL, so `deserialize_row` adds one itself.
- **Ownership.** `getline()` allocates and grows one buffer that we reuse and free once at
  exit. `strdup()` makes a copy the caller must free. ASan's leak checker catches anything
  that's forgotten.
- **Buffered stdout.** When stdout is a pipe it's fully buffered, so the prompt only shows
  up if we call `fflush`.
- **Feature-test macros.** `-std=c11` hides POSIX functions like `getline` and `strdup`;
  `-D_POSIX_C_SOURCE=200809L` brings them back without enabling GNU extensions.
- **`_Static_assert`** checks the 293-byte layout at compile time, so a mistake in a size
  constant breaks the build instead of corrupting data.

### Trickiest part

Validating the id. `strtoul("-1")` succeeds and wraps to 4294967295, and `"12abc"` parses
as 12. Checking that every character is a digit, then checking `ERANGE` and the 32-bit
upper bound, covers all the bad cases.

### Interview questions

1. **Why not `fwrite(&row, sizeof row, 1, f)`?**
   Because the struct includes padding bytes (and its layout can differ between compilers
   and platforms), the file would contain 296 bytes of which 3 are garbage. Explicit
   serialization gives a stable 293-byte format.
2. **What do AddressSanitizer and UndefinedBehaviorSanitizer catch?**
   ASan finds out-of-bounds reads and writes, use-after-free, double free and leaks by
   instrumenting memory accesses. UBSan finds undefined behaviour such as signed overflow,
   misaligned access and invalid shifts. `-fno-sanitize-recover=all` makes UBSan abort so
   the test fails.
3. **Why zero-fill the unused part of a string field?**
   So the same row always produces the same bytes. Otherwise leftover memory leaks onto
   disk, and checksums (used later by the WAL) of identical rows would differ.
4. **What's the difference between `strlen` and `strnlen`, and why use the second here?**
   `strnlen` stops after a maximum number of bytes, so it can't run off the end of a buffer
   that has no NUL terminator.
5. **Why does the Makefile use `-MMD -MP`?**
   They make gcc write a `.d` file listing every header each `.c` file includes. Make
   includes those files, so changing a header rebuilds every object that uses it.
   `-MP` adds dummy targets so deleting a header doesn't break the build.

## Phase 2: Simple pager and persistence

### What was built

- `pager.c`: the only module that touches the database file. It opens (or creates) the
  file, hands out 4KB page buffers on request, loading them from byte `N * 4096` the first
  time, and writes them back when asked.
- The table now gets its pages from the pager. On close every page is written back, and
  the last page is written only up to its last row, so on the next start the row count can
  be worked out from the file length.
- Shared pytest fixtures: `repl(commands)` starts a fresh process on a temporary database
  file, so calling it twice in one test is a restart.

### Key C concepts

- **File descriptors vs `FILE *`.** `open`/`pread`/`pwrite` work on raw descriptors with
  no hidden buffering in libc, so we control exactly when bytes reach the kernel. That
  matters later for the WAL and `fsync`.
- **`pread`/`pwrite`** take the file offset as an argument, so there's no separate `lseek`
  and no shared "current position" to get wrong.
- **`off_t` and overflow.** `page_num * PAGE_SIZE` is computed in 32 bits unless one side
  is cast to `off_t` first. With large page numbers the 32-bit result would silently wrap.
- **Short reads and writes.** `pread` can return fewer bytes than asked for (here, at the
  end of a partial last page). `calloc` zero-fills the buffer, so the missing part reads
  as zeros. A short `pwrite` is treated as an error.
- **`die()` on I/O errors.** Carrying on after a failed write would mean the in-memory
  pages and the file disagree, which is worse than stopping.

### Trickiest part

Working out how many rows exist after a restart without storing a count anywhere. Writing
every page at full size (the first version) made the file length useless, so every session
started from zero and overwrote old rows. Writing the last page only up to its last row
fixes it, and `rows_in_file` also rejects a length that would leave a partial row.

### Interview questions

1. **Why does a database use fixed-size pages instead of reading rows one at a time?**
   Disks and the OS move data in blocks (often 4KB). Reading one page fetches many rows in
   one I/O, and a fixed size makes "where is page N" a multiplication.
2. **What's the difference between `write()` returning and the data being on disk?**
   `write` only copies data into the kernel's page cache. It reaches the disk later, unless
   you call `fsync`. A crash before then can lose it. (The WAL in phase 8 relies on this.)
3. **Why compute offsets as `(off_t)page_num * PAGE_SIZE`?**
   Without the cast, the multiplication happens in 32-bit unsigned arithmetic and wraps
   above 4GB. The cast makes it 64-bit.
4. **What is the pager's job, in one sentence?**
   It hides the file. The rest of the database asks for page N and gets a memory buffer,
   while the pager decides when bytes move between memory and disk.
5. **What's wrong with storing the row count only in the file length?**
   It only works while rows are appended and the last page is the only partial one. Any
   layout with real metadata (like B+tree nodes, next phase) needs the count stored in the
   page itself.

## Phase 3: Single-leaf B+tree and cursor

### What was built

- `btree.c`: the on-disk node layout. Page 0 is a leaf with a 14-byte header
  (node_type, is_root, parent, num_cells, next_leaf) followed by up to 13 cells of
  key(4) + row(293). Every field has a named offset and a getter/setter.
- Inserts use a binary search to find the right cell, shift later cells right with
  `memmove`, and keep the leaf sorted by key. Duplicate keys are rejected with
  `Error: Duplicate key N.`
- `cursor.c`: a cursor is a (page, cell) position. `table_start` and `table_find` create
  one, `cursor_advance` moves it, `cursor_value` reads the row under it. `select` is now
  just a cursor loop, so it won't need changing when the tree grows.
- The database file is now always whole pages; anything else is reported as corrupt.

### Key C concepts

- **Alignment and strict aliasing.** `*(uint32_t *)(page + 6)` reads a 4-byte integer
  from an address that isn't a multiple of 4. That's undefined behaviour in C (UBSan
  reports it, and some CPUs crash). It also breaks strict-aliasing rules. `memcpy` into a
  local variable is always defined, and gcc compiles it to the same single instruction.
- **`memmove` vs `memcpy`.** Shifting cells right copies a region onto an overlapping
  region. `memcpy` is undefined for overlapping buffers; `memmove` handles them.
- **`_Static_assert`** pins the layout (14-byte header, 13 cells per leaf) so a wrong
  constant breaks the build instead of silently changing the file format.
- **Returning structs by value.** A `Cursor` is 16 bytes, so returning it by value is
  cheap and means there's no `malloc`/`free` to forget.
- **Designated initialisers** (`.page_num = ...`) make it obvious which field gets which
  value, and zero every field that isn't mentioned.

### Trickiest part

Getting the binary search exactly right. It has to return the key's index if it exists,
and otherwise the index where the key *would* go, including position 0 and position
`num_cells`. Searching a half-open range `[low, high)` with a clear invariant (everything
before `low` is smaller, everything from `high` on is bigger or equal) avoids off-by-one
mistakes. Computing `mid` as `low + (high - low) / 2` avoids overflow.

### Interview questions

1. **Why are B+tree nodes the same size as a page?**
   One node means one I/O. The tree's height (and so the number of disk reads per lookup)
   depends on how many keys fit in a node, so filling a whole page maximises fan-out.
2. **What's the difference between a B-tree and a B+tree?**
   In a B+tree, rows live only in the leaves. Internal nodes hold just keys and child
   pointers, so more of them fit per page. The leaves are also linked, which makes range
   scans a simple walk.
3. **Why use binary search inside a node when it only has 13 cells?**
   Internal nodes will hold hundreds of keys. The same sorted-array search works for both
   node types, and it's O(log n) instead of O(n).
4. **Why does `(low + high) / 2` have a bug?**
   With unsigned 32-bit values, `low + high` can overflow past 2^32 and wrap, giving a
   `mid` that's out of range. `low + (high - low) / 2` can't overflow.
5. **Why check for a duplicate key before checking if the leaf is full?**
   If the key already exists, "duplicate key" is the true reason the insert can't happen.
   Reporting "table full" would be misleading, and after splits exist a full leaf won't
   even be an error.

## Phase 4: Leaf splits and root splits

### What was built

- `.btree`, a meta command that prints the tree with page numbers, so tests can check the
  exact shape of the tree and which page holds what.
- The internal node layout: a 14-byte header (node_type, is_root, parent, num_keys,
  right_child) and up to 510 cells of child(4) + key(4). Key `i` is the largest key under
  child `i`; anything bigger lives under `right_child`.
- Leaf splits. When a full leaf gets a 14th cell, the 14 cells are shared 7/7 between the
  old leaf and a new leaf at the end of the file. The new leaf is linked in through
  `next_leaf` right after the old one.
- Root splits. Page 0 must stay the root, so its contents (the left half) are copied to a
  new page, and page 0 is rewritten as an internal node with one key and two children.
- `table_find` now walks down through internal nodes using a binary search on their keys.
  The cursor follows `next_leaf` from one leaf to the next, so `select` still returns rows
  in key order.
- Still missing: a leaf below the root can't split yet (it needs its parent updated), so
  sequential inserts stop at 20 rows with `Error: Table full.`

### Key C concepts

- **Overlapping copies.** In the split loop some cells are "copied" onto themselves.
  `memcpy` with overlapping (or identical) source and destination is undefined behaviour,
  and ASan reports it as `memcpy-param-overlap`. `memmove` is defined for that case.
- **Counting down with unsigned integers.** `for (uint32_t i = n; i >= 0; i--)` never ends,
  because an unsigned value is always `>= 0` and wraps from 0 to 4294967295.
  `for (uint32_t pos = n + 1; pos-- > 0;)` tests the value before decrementing it, so it
  visits `n`..0 and stops.
- **Sentinel values.** `NO_NEXT_LEAF` is 0 (safe because page 0 is always the root, never
  a next leaf) and an empty internal node's right child is `UINT32_MAX`. A zeroed pointer
  would point at the root and turn a bug into an infinite loop.
- **Recursion over on-disk structures.** `print_tree` and `get_node_max_key` recurse by
  page number. Each call asks the pager for the page, so the recursion depth is the tree
  height, not the number of rows.
- **Fail fast on impossible states.** An out-of-range child index stops the program instead
  of quietly reading some unrelated page.

### Trickiest part

The split loop. It walks 14 logical positions from the top down. Each position works out
which leaf it belongs to, its index in that leaf, and whether it's the new cell or an old
cell that has shifted up by one. Going downwards matters: cells that shift up inside the
old leaf land on slots that have already been emptied. The boundary tests insert the new
key at positions 0, 6, 7 and 13 (first, end of the left half, start of the right half,
last), because those are exactly where an off-by-one would put a cell in the wrong half.

### Interview questions

1. **Why split 7/7 instead of moving just one cell into the new leaf?**
   An even split leaves room in both leaves, so the next few inserts don't split again.
   A lopsided split would trigger a new split on almost every insert.
2. **Why copy the old root to a new page instead of putting the new root on a new page?**
   Then the root is always page 0, so nothing (the table, the file format) has to store
   where the root is. Only the two children move, and their parent is always 0.
3. **What does key `i` in an internal node mean here?**
   It's the largest key in child `i`'s subtree. A lookup takes the first child whose key is
   `>= ` the search key, or the right child if there is none.
4. **Why link leaves with `next_leaf`?**
   A full scan or range query can then walk the leaves left to right without going back up
   the tree. That's the main reason B+trees keep rows only in leaves.
5. **What's the cost of a lookup in this tree?**
   One page read per level. With 510 keys per internal node and 13 rows per leaf, three
   levels hold about 3.4 million rows, so a lookup touches at most three pages.
