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
