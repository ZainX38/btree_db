#include "cursor.h"

Cursor table_find(Table *table, uint32_t key) {
    Pager *pager = table->pager;
    uint32_t page_num = table->root_page_num;
    void *node = pager_get_page(pager, page_num);

    /* Walk down from the root, choosing at each internal node the one child
     * whose key range can contain `key`. Only one page is pinned at a time. */
    while (get_node_type(node) == NODE_INTERNAL) {
        uint32_t child_page_num = internal_node_child(node, internal_node_find_child(node, key));
        pager_unpin(pager, page_num);
        page_num = child_page_num;
        node = pager_get_page(pager, page_num);
    }

    uint32_t cell_num = leaf_node_find_cell(node, key);
    uint32_t num_cells = leaf_node_num_cells(node);
    pager_unpin(pager, page_num);
    Cursor cursor = {
        .table = table,
        .page_num = page_num,
        .cell_num = cell_num,
        /* A position past the last cell is only meaningful as an insert point. */
        .end_of_table = cell_num >= num_cells,
    };
    return cursor;
}

Cursor table_start(Table *table) {
    /* No key is smaller than 0, so searching for it lands on the first cell
     * of the left-most leaf. */
    Cursor cursor = table_find(table, 0);
    void *leaf = pager_get_page(table->pager, cursor.page_num);
    /* An empty table starts at its own end, so a scan loop runs zero times. */
    cursor.cell_num = 0;
    cursor.end_of_table = leaf_node_num_cells(leaf) == 0;
    pager_unpin(table->pager, cursor.page_num);
    return cursor;
}

void cursor_read_row(const Cursor *cursor, Row *row) {
    Pager *pager = cursor->table->pager;
    void *page = pager_get_page(pager, cursor->page_num);
    deserialize_row(leaf_node_value(page, cursor->cell_num), row);
    pager_unpin(pager, cursor->page_num);
}

bool cursor_holds_key(const Cursor *cursor, uint32_t key) {
    Pager *pager = cursor->table->pager;
    void *page = pager_get_page(pager, cursor->page_num);
    bool holds = cursor->cell_num < leaf_node_num_cells(page) &&
                 leaf_node_key(page, cursor->cell_num) == key;
    pager_unpin(pager, cursor->page_num);
    return holds;
}

void cursor_advance(Cursor *cursor) {
    Pager *pager = cursor->table->pager;
    void *node = pager_get_page(pager, cursor->page_num);
    uint32_t num_cells = leaf_node_num_cells(node);
    uint32_t next_page_num = leaf_node_next_leaf(node);
    pager_unpin(pager, cursor->page_num);

    cursor->cell_num++;
    if (cursor->cell_num < num_cells) {
        return;
    }
    /* Leaves are chained in key order, so a full scan never has to go back
     * up through the internal nodes. */
    if (next_page_num == NO_NEXT_LEAF) {
        cursor->end_of_table = true;
        return;
    }
    cursor->page_num = next_page_num;
    cursor->cell_num = 0;
}
