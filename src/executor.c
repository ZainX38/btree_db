#include "executor.h"

#include "cursor.h"

static ExecuteResult execute_insert(const Statement *statement, Table *table) {
    const Row *row = &statement->row_to_insert;
    void *root = pager_get_page(table->pager, table->root_page_num);
    uint32_t num_cells = leaf_node_num_cells(root);

    /* Check for a duplicate before checking for space, so re-inserting an
     * existing key reports the more useful error even when the leaf is full. */
    Cursor cursor = table_find(table, row->id);
    if (cursor.cell_num < num_cells && leaf_node_key(root, cursor.cell_num) == row->id) {
        return EXECUTE_DUPLICATE_KEY;
    }
    /* A single leaf can't split yet, so a full leaf means a full table. */
    if (num_cells >= LEAF_NODE_MAX_CELLS) {
        return EXECUTE_TABLE_FULL;
    }
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
