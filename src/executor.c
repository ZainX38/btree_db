#include "executor.h"

#include <stdio.h>
#include <stdlib.h>

Table *db_open(const char *filename) {
    Table *table = calloc(1, sizeof(Table));
    if (table == NULL) {
        perror("calloc");
        exit(EXIT_FAILURE);
    }
    table->pager = pager_open(filename);
    /* The row count isn't stored anywhere yet, so every session starts empty
     * and writes over what the last one left. */
    table->num_rows = 0;
    return table;
}

void db_close(Table *table) {
    Pager *pager = table->pager;
    for (uint32_t i = 0; i < pager->num_pages; i++) {
        pager_flush(pager, i, PAGE_SIZE);
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
