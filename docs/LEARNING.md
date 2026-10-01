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
