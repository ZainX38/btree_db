#ifndef EXECUTOR_H
#define EXECUTOR_H

/*
 * Runs prepared statements against a table.
 *
 * The table is still an append-only array of rows, but its pages now come
 * from the pager, so they live in the database file.
 */

#include <stdint.h>

#include "pager.h"
#include "parser.h"

/* Rows never straddle a page boundary; the leftover bytes at the end of each
 * page are wasted, which keeps "find row N" a simple division. */
#define ROWS_PER_PAGE (PAGE_SIZE / ROW_SIZE)
#define TABLE_MAX_ROWS (ROWS_PER_PAGE * PAGER_MAX_PAGES)

typedef struct {
    uint32_t num_rows;
    Pager *pager;
} Table;

typedef enum { EXECUTE_SUCCESS, EXECUTE_TABLE_FULL } ExecuteResult;

Table *db_open(const char *filename);
/* Writes every page back to disk, then releases the table. */
void db_close(Table *table);
ExecuteResult execute_statement(const Statement *statement, Table *table);

#endif
