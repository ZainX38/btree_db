#include "executor.h"

#include <string.h>

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

/* Turns a three-way comparison result (<0, 0, >0) into the operator's answer. */
static bool compare_holds(CompareOp op, int cmp) {
    switch (op) {
    case COMPARE_EQ:
        return cmp == 0;
    case COMPARE_NE:
        return cmp != 0;
    case COMPARE_LT:
        return cmp < 0;
    case COMPARE_LE:
        return cmp <= 0;
    case COMPARE_GT:
        return cmp > 0;
    case COMPARE_GE:
        return cmp >= 0;
    }
    return false;
}

static int compare_ids(uint32_t a, uint32_t b) {
    return (a > b) - (a < b);
}

static bool row_matches(const Row *row, const WhereClause *where) {
    if (!where->present) {
        return true;
    }
    switch (where->column) {
    case COLUMN_ID:
        return compare_holds(where->op, compare_ids(row->id, where->id));
    case COLUMN_USERNAME:
        return compare_holds(where->op, strcmp(row->username, where->text));
    case COLUMN_EMAIL:
        return compare_holds(where->op, strcmp(row->email, where->text));
    }
    return false;
}

/* The id is the tree's key, so a lower bound on it lets the scan start with
 * one descent instead of walking every leaf before the first match. Parent
 * keys are their child's largest key, so the descent only lands past the end
 * of a leaf when the bound is beyond every row, and the scan is rightly empty. */
static Cursor scan_start(Table *table, const WhereClause *where) {
    bool has_lower_bound = where->present && where->column == COLUMN_ID &&
                           (where->op == COMPARE_EQ || where->op == COMPARE_GT ||
                            where->op == COMPARE_GE);
    return has_lower_bound ? table_find(table, where->id) : table_start(table);
}

/* Rows come out in id order, so once an upper bound on the id is passed no
 * later row can match and the rest of the leaves can be skipped. */
static bool past_upper_bound(const Row *row, const WhereClause *where) {
    bool has_upper_bound = where->present && where->column == COLUMN_ID &&
                           (where->op == COMPARE_EQ || where->op == COMPARE_LT ||
                            where->op == COMPARE_LE);
    return has_upper_bound && row->id > where->id;
}

static ExecuteResult execute_select(const Statement *statement, Table *table) {
    const WhereClause *where = &statement->where;
    Row row;
    for (Cursor cursor = scan_start(table, where); !cursor.end_of_table;
         cursor_advance(&cursor)) {
        cursor_read_row(&cursor, &row);
        if (past_upper_bound(&row, where)) {
            break;
        }
        if (row_matches(&row, where)) {
            print_row(&row);
        }
    }
    return EXECUTE_SUCCESS;
}

ExecuteResult execute_statement(const Statement *statement, Table *table) {
    switch (statement->type) {
    case STATEMENT_INSERT:
        return execute_insert(statement, table);
    case STATEMENT_SELECT:
        return execute_select(statement, table);
    }
    return EXECUTE_SUCCESS;
}
