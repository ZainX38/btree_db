#include "executor.h"

#include <stdio.h>
#include <stdlib.h>

/* There is no header storing the row count, so it is worked out from the file
 * length: full pages hold ROWS_PER_PAGE rows each and only the last page may
 * be partial. This goes away once the B+tree stores counts in its nodes. */
static uint32_t rows_in_file(uint32_t file_length) {
    uint32_t full_pages = file_length / PAGE_SIZE;
    uint32_t tail_bytes = file_length % PAGE_SIZE;
    if (tail_bytes % ROW_SIZE != 0) {
        fprintf(stderr, "Database file is corrupt: last page holds a partial row.\n");
        exit(EXIT_FAILURE);
    }
    return full_pages * ROWS_PER_PAGE + tail_bytes / ROW_SIZE;
}

Table *db_open(const char *filename) {
    Table *table = calloc(1, sizeof(Table));
    if (table == NULL) {
        perror("calloc");
        exit(EXIT_FAILURE);
    }
    table->pager = pager_open(filename);
    table->num_rows = rows_in_file(table->pager->file_length);
    return table;
}

void db_close(Table *table) {
    Pager *pager = table->pager;
    uint32_t full_pages = table->num_rows / ROWS_PER_PAGE;
    for (uint32_t i = 0; i < full_pages; i++) {
        pager_flush(pager, i, PAGE_SIZE);
    }
    /* Write the last page only up to its last row, so the file length still
     * tells the next session how many rows there are. */
    uint32_t extra_rows = table->num_rows % ROWS_PER_PAGE;
    if (extra_rows > 0) {
        pager_flush(pager, full_pages, extra_rows * ROW_SIZE);
    }
    pager_close(pager);
    free(table);
}

/* Returns where row number `row_num` lives inside its cached page. */
static void *row_slot(Table *table, uint32_t row_num) {
    uint32_t page_num = row_num / ROWS_PER_PAGE;
    void *page = pager_get_page(table->pager, page_num);
    uint32_t byte_offset = (row_num % ROWS_PER_PAGE) * ROW_SIZE;
    return (char *)page + byte_offset;
}

static ExecuteResult execute_insert(const Statement *statement, Table *table) {
    if (table->num_rows >= TABLE_MAX_ROWS) {
        return EXECUTE_TABLE_FULL;
    }
    serialize_row(&statement->row_to_insert, row_slot(table, table->num_rows));
    table->num_rows++;
    return EXECUTE_SUCCESS;
}

static ExecuteResult execute_select(Table *table) {
    Row row;
    for (uint32_t i = 0; i < table->num_rows; i++) {
        deserialize_row(row_slot(table, i), &row);
        print_row(&row);
    }
    return EXECUTE_SUCCESS;
}

ExecuteResult execute_statement(const Statement *statement, Table *table) {
    switch (statement->type) {
    case STATEMENT_INSERT:
        return execute_insert(statement, table);
    case STATEMENT_SELECT:
        return execute_select(table);
    }
    return EXECUTE_SUCCESS;
}
