#include "executor.h"

static ExecuteResult execute_insert(const Statement *statement, Table *table) {
    void *root = pager_get_page(table->pager, table->root_page_num);
    /* A single leaf can't split yet, so a full leaf means a full table. */
    if (leaf_node_num_cells(root) >= LEAF_NODE_MAX_CELLS) {
        return EXECUTE_TABLE_FULL;
    }
    const Row *row = &statement->row_to_insert;
    leaf_node_append(root, row->id, row);
    return EXECUTE_SUCCESS;
}

static ExecuteResult execute_select(Table *table) {
    void *root = pager_get_page(table->pager, table->root_page_num);
    uint32_t num_cells = leaf_node_num_cells(root);
    Row row;
    for (uint32_t i = 0; i < num_cells; i++) {
        deserialize_row(leaf_node_value(root, i), &row);
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
