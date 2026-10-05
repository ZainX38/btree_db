#ifndef EXECUTOR_H
#define EXECUTOR_H

/*
 * Runs prepared statements against the B+tree. The row id doubles as the
 * tree's key.
 */

#include "btree.h"
#include "parser.h"

typedef enum { EXECUTE_SUCCESS, EXECUTE_TABLE_FULL, EXECUTE_DUPLICATE_KEY } ExecuteResult;

ExecuteResult execute_statement(const Statement *statement, Table *table);

#endif
