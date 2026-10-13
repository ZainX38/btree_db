#ifndef PARSER_H
#define PARSER_H

/*
 * Turns one line of input into a Statement.
 *
 * The grammar is still the short REPL form ("insert <id> <username> <email>"
 * / "select"), but it now works on tokens, so keywords are case-insensitive
 * and strings can be quoted to hold spaces. A recursive descent parser for
 * real SQL replaces it next.
 */

#include "row.h"

typedef enum { STATEMENT_INSERT, STATEMENT_SELECT } StatementType;

typedef struct {
    StatementType type;
    Row row_to_insert; /* only meaningful for STATEMENT_INSERT */
} Statement;

typedef enum {
    PREPARE_SUCCESS,
    PREPARE_SYNTAX_ERROR,
    PREPARE_UNRECOGNIZED_STATEMENT,
    PREPARE_INVALID_ID,
    PREPARE_STRING_TOO_LONG,
} PrepareResult;

PrepareResult prepare_statement(const char *input, Statement *statement);

#endif
