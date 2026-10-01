#ifndef PARSER_H
#define PARSER_H

/*
 * Turns one line of input into a Statement.
 *
 * This is a deliberately simple whitespace-split parser
 * ("insert <id> <username> <email>" / "select") so the storage layers can be
 * built and tested first. It gets replaced by a tokenizer and a recursive
 * descent SQL parser in phase 7.
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

/* Note: tokenizes `input` in place, so the caller's buffer is modified. */
PrepareResult prepare_statement(char *input, Statement *statement);

#endif
