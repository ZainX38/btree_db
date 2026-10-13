#ifndef PARSER_H
#define PARSER_H

/*
 * Turns one line of input into a Statement with a recursive descent parser:
 * one function per grammar rule, each consuming the tokens it recognises.
 *
 *   statement := (insert | select) [";"] END
 *   insert    := INSERT ( INTO name VALUES "(" value "," value "," value ")"
 *                       | value value value )
 *   select    := SELECT [ "*" FROM name ] [ WHERE column op value ]
 *   op        := "=" | "!=" | "<" | "<=" | ">" | ">="
 *   value     := word | 'quoted string'
 *
 * The short forms ("insert 1 amy a@x", "select") are kept as REPL shorthand
 * because they are much quicker to type when poking at the tree by hand.
 */

#include <stdbool.h>

#include "row.h"

#define TABLE_NAME "users"

typedef enum { STATEMENT_INSERT, STATEMENT_SELECT } StatementType;

typedef enum { COLUMN_ID, COLUMN_USERNAME, COLUMN_EMAIL } Column;

typedef enum {
    COMPARE_EQ,
    COMPARE_NE,
    COMPARE_LT,
    COMPARE_LE,
    COMPARE_GT,
    COMPARE_GE,
} CompareOp;

typedef struct {
    bool present;
    Column column;
    CompareOp op;
    uint32_t id;                  /* the value when column is COLUMN_ID */
    char text[EMAIL_MAX_LEN + 1]; /* the value for the string columns */
} WhereClause;

typedef struct {
    StatementType type;
    Row row_to_insert; /* only meaningful for STATEMENT_INSERT */
    WhereClause where; /* only meaningful for STATEMENT_SELECT */
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
