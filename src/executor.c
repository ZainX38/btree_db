#include "executor.h"

#include "cursor.h"

static ExecuteResult execute_insert(const Statement *statement, Table *table) {
    void *root = pager_get_page(table->pager, table->root_page_num);
    /* A single leaf can't split yet, so a full leaf means a full table. */
    if (leaf_node_num_cells(root) >= LEAF_NODE_MAX_CELLS) {
        return EXECUTE_TABLE_FULL;
    }
    const Row *row = &statement->row_to_insert;
    Cursor cursor = table_end(table);
    leaf_node_insert(root, cursor.cell_num, row->id, row);
    return EXECUTE_SUCCESS;
}

static ExecuteResult execute_select(Table *table) {
    Row row;
    for (Cursor cursor = table_start(table); !cursor.end_of_table; cursor_advance(&cursor)) {
        deserialize_row(cursor_value(&cursor), &row);
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
