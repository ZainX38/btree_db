#include "crashpoint.h"

#ifdef NDEBUG

void crash_point(void) {}

#else

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Writes left before the crash, or -1 when no crash was asked for. */
static long writes_left = -1;
static bool configured = false;

/* Read once, on the first write, so the REPL needs no setup call. Anything
 * that isn't a plain number is ignored rather than crashing at random. */
static void configure(void) {
    configured = true;
    const char *value = getenv(CRASH_AFTER_ENV);
    if (value == NULL || *value == '\0' || strspn(value, "0123456789") != strlen(value)) {
        return;
    }
    writes_left = strtol(value, NULL, 10);
}

void crash_point(void) {
    if (!configured) {
        configure();
    }
    if (writes_left < 0) {
        return;
    }
    if (writes_left == 0) {
        _exit(CRASH_EXIT_CODE);
    }
    writes_left--;
}

#endif
