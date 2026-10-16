#ifndef CRASHPOINT_H
#define CRASHPOINT_H

/*
 * Test-only fault injection. Every write to the database file or the WAL
 * calls crash_point() first. With BTREE_DB_CRASH_AFTER=N in the environment,
 * the first N writes go through and the next one kills the process with
 * _exit() instead, which skips every cleanup, as kill -9 or a power cut would.
 *
 * A test can then try every N in turn and so crash at every write a script
 * makes, instead of hoping a randomly timed kill lands somewhere interesting.
 * Release builds (NDEBUG) compile it to nothing, so the variable has no effect
 * outside tests.
 */

#define CRASH_AFTER_ENV "BTREE_DB_CRASH_AFTER"
/* Distinct from EXIT_FAILURE so tests can tell a planned crash from an error. */
#define CRASH_EXIT_CODE 86

void crash_point(void);

#endif
