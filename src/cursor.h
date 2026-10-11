#ifndef CURSOR_H
#define CURSOR_H

/*
 * A cursor is a position in the table: which leaf page, and which cell inside
 * it. The executor only moves cursors around, so it never needs to know how
 * cells are laid out or how leaves are linked together.
 *
 * Cursors are small values, passed and returned by value, so there is nothing
 * to free. A cursor holds no page pins between calls: each call pins its leaf,
 * reads what it needs and unpins, so an open cursor never stops eviction.
 */

#include <stdbool.h>
#include <stdint.h>

#include "btree.h"
#include "row.h"

typedef struct {
    Table *table;
    uint32_t page_num;
    uint32_t cell_num;
    /* True when the cursor sits one past the last row. */
    bool end_of_table;
} Cursor;

Cursor table_start(Table *table);
/* Positions the cursor on `key`, or where `key` would be inserted. */
Cursor table_find(Table *table, uint32_t key);
/* Copies the row under the cursor into `row`. */
void cursor_read_row(const Cursor *cursor, Row *row);
/* True if the cursor sits on a cell whose key is `key`. */
bool cursor_holds_key(const Cursor *cursor, uint32_t key);
void cursor_advance(Cursor *cursor);

#endif
