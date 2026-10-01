#ifndef EXECUTOR_H
#define EXECUTOR_H

/*
 * Runs prepared statements against a table.
 *
 * For now the table is an append-only array of rows packed into 4KB pages held
 * in memory. Rows are already grouped into pages so the pager (phase 2) can
 * take over storage without changing how rows are laid out.
 */

#include <stdint.h>

#include "parser.h"

#define PAGE_SIZE 4096u
#define TABLE_MAX_PAGES 100u
/* Rows never straddle a page boundary; the leftover bytes at the end of each
 * page are wasted, which keeps "find row N" a simple division. */
#define ROWS_PER_PAGE (PAGE_SIZE / ROW_SIZE)
#define TABLE_MAX_ROWS (ROWS_PER_PAGE * TABLE_MAX_PAGES)

typedef struct {
    uint32_t num_rows;
    void *pages[TABLE_MAX_PAGES]; /* allocated lazily, NULL until first use */
} Table;

typedef enum { EXECUTE_SUCCESS, EXECUTE_TABLE_FULL } ExecuteResult;

Table *table_new(void);
void table_free(Table *table);
ExecuteResult execute_statement(const Statement *statement, Table *table);

#endif
