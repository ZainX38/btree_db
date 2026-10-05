#include "cursor.h"

static uint32_t root_num_cells(Table *table) {
    return leaf_node_num_cells(pager_get_page(table->pager, table->root_page_num));
}

Cursor table_start(Table *table) {
    Cursor cursor = {
        .table = table,
        .page_num = table->root_page_num,
        .cell_num = 0,
    };
    /* An empty table starts at its own end, so a scan loop runs zero times. */
    cursor.end_of_table = root_num_cells(table) == 0;
    return cursor;
}

Cursor table_end(Table *table) {
    Cursor cursor = {
        .table = table,
        .page_num = table->root_page_num,
        .cell_num = root_num_cells(table),
        .end_of_table = true,
    };
    return cursor;
}

void *cursor_value(const Cursor *cursor) {
    void *page = pager_get_page(cursor->table->pager, cursor->page_num);
    return leaf_node_value(page, cursor->cell_num);
}

void cursor_advance(Cursor *cursor) {
    void *node = pager_get_page(cursor->table->pager, cursor->page_num);
    cursor->cell_num++;
    if (cursor->cell_num >= leaf_node_num_cells(node)) {
        cursor->end_of_table = true;
    }
}
