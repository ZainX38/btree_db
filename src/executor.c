#include "executor.h"

#include "cursor.h"

static ExecuteResult execute_insert(const Statement *statement, Table *table) {
    const Row *row = &statement->row_to_insert;
    Cursor cursor = table_find(table, row->id);
    if (cursor_holds_key(&cursor, row->id)) {
        return EXECUTE_DUPLICATE_KEY;
    }
    leaf_node_insert(table, cursor.page_num, cursor.cell_num, row->id, row);
    return EXECUTE_SUCCESS;
}

static ExecuteResult execute_select(Table *table) {
    Row row;
    for (Cursor cursor = table_start(table); !cursor.end_of_table; cursor_advance(&cursor)) {
        cursor_read_row(&cursor, &row);
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
