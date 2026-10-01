/*
 * Entry point for btree_db: a read-eval-print loop over stdin.
 *
 * Lines starting with '.' are meta commands handled here; everything else will
 * become a SQL statement once the parser exists.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define PROMPT "db > "

/* getline() owns and grows the buffer, so we keep it across iterations
 * instead of allocating a new one per line. */
typedef struct {
    char *buffer;
    size_t capacity;
    ssize_t length;
} InputBuffer;

typedef enum { META_COMMAND_EXIT, META_COMMAND_UNRECOGNIZED } MetaCommandResult;

static void print_prompt(void) {
    printf(PROMPT);
    /* stdout is fully buffered when piped (as in tests), so without this the
     * prompt would only appear after the process exits. */
    fflush(stdout);
}

/* Returns false on end of input, which we treat like .exit so piped scripts
 * that forget .exit still shut down cleanly. */
static bool read_input(InputBuffer *input) {
    input->length = getline(&input->buffer, &input->capacity, stdin);
    if (input->length < 0) {
        return false;
    }
    if (input->length > 0 && input->buffer[input->length - 1] == '\n') {
        input->buffer[--input->length] = '\0';
    }
    return true;
}

static MetaCommandResult do_meta_command(const InputBuffer *input) {
    if (strcmp(input->buffer, ".exit") == 0) {
        return META_COMMAND_EXIT;
    }
    return META_COMMAND_UNRECOGNIZED;
}

int main(void) {
    InputBuffer input = {0};

    for (;;) {
        print_prompt();
        if (!read_input(&input)) {
            break;
        }
        if (input.length == 0) {
            continue;
        }
        if (input.buffer[0] == '.') {
            if (do_meta_command(&input) == META_COMMAND_EXIT) {
                break;
            }
            printf("Unrecognized command '%s'.\n", input.buffer);
            continue;
        }
        printf("Unrecognized keyword at start of '%s'.\n", input.buffer);
    }

    free(input.buffer);
    return EXIT_SUCCESS;
}
