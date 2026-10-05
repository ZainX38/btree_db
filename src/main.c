/*
 * Entry point for btree_db: a read-eval-print loop over stdin.
 *
 * Lines starting with '.' are meta commands handled here; everything else is
 * prepared into a Statement by the parser and run by the executor.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "btree.h"
#include "executor.h"
#include "parser.h"

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

/* Each failure gets its own message so tests (and users) can tell exactly
 * which rule an input broke. */
static void report_prepare_error(PrepareResult result, const char *input) {
    switch (result) {
    case PREPARE_SYNTAX_ERROR:
        printf("Syntax error. Could not parse statement.\n");
        break;
    case PREPARE_INVALID_ID:
        printf("ID must be an integer between 0 and %u.\n", UINT32_MAX);
        break;
    case PREPARE_STRING_TOO_LONG:
        printf("String is too long.\n");
        break;
    case PREPARE_UNRECOGNIZED_STATEMENT:
        printf("Unrecognized keyword at start of '%s'.\n", input);
        break;
    case PREPARE_SUCCESS:
        break;
    }
}

static void report_execute_result(ExecuteResult result, const Statement *statement) {
    switch (result) {
    case EXECUTE_SUCCESS:
        printf("Executed.\n");
        break;
    case EXECUTE_TABLE_FULL:
        printf("Error: Table full.\n");
        break;
    case EXECUTE_DUPLICATE_KEY:
        printf("Error: Duplicate key %u.\n", statement->row_to_insert.id);
        break;
    }
}

static void run_statement(InputBuffer *input, Table *table) {
    /* prepare_statement tokenizes in place, so keep a copy for error messages. */
    char *original = strdup(input->buffer);
    if (original == NULL) {
        perror("strdup");
        exit(EXIT_FAILURE);
    }
    Statement statement;
    PrepareResult prepared = prepare_statement(input->buffer, &statement);
    if (prepared != PREPARE_SUCCESS) {
        report_prepare_error(prepared, original);
    } else {
        report_execute_result(execute_statement(&statement, table), &statement);
    }
    free(original);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <database file>\n", argv[0]);
        return EXIT_FAILURE;
    }
    InputBuffer input = {0};
    Table *table = db_open(argv[1]);

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
        run_statement(&input, table);
    }

    db_close(table);
    free(input.buffer);
    return EXIT_SUCCESS;
}
