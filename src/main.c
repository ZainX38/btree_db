/*
 * Entry point for btree_db: a read-eval-print loop over stdin.
 *
 * Lines starting with '.' are meta commands handled here; everything else is
 * prepared into a Statement by the parser and run by the executor.
 */
#include <inttypes.h>
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

typedef enum {
    META_COMMAND_SUCCESS,
    META_COMMAND_EXIT,
    META_COMMAND_UNRECOGNIZED,
} MetaCommandResult;

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

/* One "name: value" per line so tests can parse it into a dict. */
static void print_cache_stats(const Pager *pager) {
    printf("frames: %u\n", pager->num_frames);
    printf("pages: %u\n", pager->num_pages);
    printf("hits: %" PRIu64 "\n", pager->hits);
    printf("misses: %" PRIu64 "\n", pager->misses);
    printf("evictions: %" PRIu64 "\n", pager->evictions);
    printf("peak pinned: %u\n", pager->peak_pinned_pages);
}

static MetaCommandResult do_meta_command(const InputBuffer *input, Table *table) {
    if (strcmp(input->buffer, ".exit") == 0) {
        return META_COMMAND_EXIT;
    }
    if (strcmp(input->buffer, ".btree") == 0) {
        printf("Tree:\n");
        print_tree(table->pager, table->root_page_num, 0);
        return META_COMMAND_SUCCESS;
    }
    if (strcmp(input->buffer, ".validate") == 0) {
        validate_tree(table);
        return META_COMMAND_SUCCESS;
    }
    if (strcmp(input->buffer, ".stats") == 0) {
        print_cache_stats(table->pager);
        return META_COMMAND_SUCCESS;
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
    case EXECUTE_DUPLICATE_KEY:
        printf("Error: Duplicate key %u.\n", statement->row_to_insert.id);
        break;
    }
}

static void run_statement(const InputBuffer *input, Table *table) {
    Statement statement;
    PrepareResult prepared = prepare_statement(input->buffer, &statement);
    if (prepared != PREPARE_SUCCESS) {
        report_prepare_error(prepared, input->buffer);
    } else {
        report_execute_result(execute_statement(&statement, table), &statement);
    }
}

typedef struct {
    const char *filename;
    uint32_t cache_frames;
} Options;

/* Accepts only plain decimal digits, so "-1" or "8x" are rejected rather
 * than silently becoming a huge or truncated number. */
static bool parse_frame_count(const char *text, uint32_t *out) {
    if (*text == '\0' || strspn(text, "0123456789") != strlen(text)) {
        return false;
    }
    unsigned long value = strtoul(text, NULL, 10);
    if (value > PAGER_MAX_FRAMES) {
        return false;
    }
    *out = (uint32_t)value;
    return true;
}

/* Usage: btree_db [--cache-frames N] <database file> */
static bool parse_options(int argc, char *argv[], Options *options) {
    options->cache_frames = PAGER_DEFAULT_FRAMES;
    if (argc == 2) {
        options->filename = argv[1];
        return true;
    }
    if (argc == 4 && strcmp(argv[1], "--cache-frames") == 0) {
        options->filename = argv[3];
        return parse_frame_count(argv[2], &options->cache_frames);
    }
    return false;
}

int main(int argc, char *argv[]) {
    Options options;
    if (!parse_options(argc, argv, &options)) {
        fprintf(stderr, "Usage: %s [--cache-frames N] <database file>\n", argv[0]);
        return EXIT_FAILURE;
    }
    InputBuffer input = {0};
    Table *table = db_open(options.filename, options.cache_frames);

    for (;;) {
        print_prompt();
        if (!read_input(&input)) {
            break;
        }
        if (input.length == 0) {
            continue;
        }
        if (input.buffer[0] == '.') {
            MetaCommandResult result = do_meta_command(&input, table);
            if (result == META_COMMAND_EXIT) {
                break;
            }
            if (result == META_COMMAND_UNRECOGNIZED) {
                printf("Unrecognized command '%s'.\n", input.buffer);
            }
            continue;
        }
        run_statement(&input, table);
    }

    db_close(table);
    free(input.buffer);
    return EXIT_SUCCESS;
}
