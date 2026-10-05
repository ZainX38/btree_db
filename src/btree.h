#ifndef BTREE_H
#define BTREE_H

/*
 * On-disk B+tree node layout and the accessors that read and write it.
 *
 * Every node is one 4KB page. Fields sit at fixed byte offsets and are always
 * read and written with memcpy (see btree.c), never by casting the page to a
 * struct, because some fields are not aligned (e.g. a uint32_t at offset 6).
 *
 * For now the tree is a single leaf, which is also the root at page 0.
 */

#include <stdbool.h>
#include <stdint.h>

#include "pager.h"
#include "row.h"

typedef enum { NODE_INTERNAL = 0, NODE_LEAF = 1 } NodeType;

/* Common header, shared by every node: node_type(1) is_root(1) parent(4). */
#define NODE_TYPE_SIZE 1u
#define NODE_TYPE_OFFSET 0u
#define IS_ROOT_SIZE 1u
#define IS_ROOT_OFFSET (NODE_TYPE_OFFSET + NODE_TYPE_SIZE)
#define PARENT_POINTER_SIZE 4u
#define PARENT_POINTER_OFFSET (IS_ROOT_OFFSET + IS_ROOT_SIZE)
#define COMMON_NODE_HEADER_SIZE (NODE_TYPE_SIZE + IS_ROOT_SIZE + PARENT_POINTER_SIZE)

/* Leaf header: common header, then num_cells(4) next_leaf(4). */
#define LEAF_NODE_NUM_CELLS_SIZE 4u
#define LEAF_NODE_NUM_CELLS_OFFSET COMMON_NODE_HEADER_SIZE
#define LEAF_NODE_NEXT_LEAF_SIZE 4u
#define LEAF_NODE_NEXT_LEAF_OFFSET (LEAF_NODE_NUM_CELLS_OFFSET + LEAF_NODE_NUM_CELLS_SIZE)
#define LEAF_NODE_HEADER_SIZE                                                                     \
    (COMMON_NODE_HEADER_SIZE + LEAF_NODE_NUM_CELLS_SIZE + LEAF_NODE_NEXT_LEAF_SIZE)

/* Leaf body: an array of cells, each key(4) followed by a serialized row. */
#define LEAF_NODE_KEY_SIZE 4u
#define LEAF_NODE_KEY_OFFSET 0u
#define LEAF_NODE_VALUE_SIZE ROW_SIZE
#define LEAF_NODE_VALUE_OFFSET (LEAF_NODE_KEY_OFFSET + LEAF_NODE_KEY_SIZE)
#define LEAF_NODE_CELL_SIZE (LEAF_NODE_KEY_SIZE + LEAF_NODE_VALUE_SIZE)
#define LEAF_NODE_SPACE_FOR_CELLS (PAGE_SIZE - LEAF_NODE_HEADER_SIZE)
#define LEAF_NODE_MAX_CELLS (LEAF_NODE_SPACE_FOR_CELLS / LEAF_NODE_CELL_SIZE)

/* Page 0 is always the root, so it can never be anyone's next leaf. That
 * makes 0 a safe "no next leaf" marker. */
#define NO_NEXT_LEAF 0u

#define ROOT_PAGE_NUM 0u

typedef struct {
    Pager *pager;
    uint32_t root_page_num;
} Table;

Table *db_open(const char *filename);
/* Writes every cached page back to disk, then releases the table. */
void db_close(Table *table);

NodeType get_node_type(const void *node);
void set_node_type(void *node, NodeType type);
bool is_node_root(const void *node);
void set_node_root(void *node, bool is_root);

uint32_t leaf_node_num_cells(const void *node);
void set_leaf_node_num_cells(void *node, uint32_t num_cells);
uint32_t leaf_node_next_leaf(const void *node);
void set_leaf_node_next_leaf(void *node, uint32_t next_leaf);
uint32_t leaf_node_key(const void *node, uint32_t cell_num);
void set_leaf_node_key(void *node, uint32_t cell_num, uint32_t key);
/* Returns a pointer to the serialized row stored in a cell. */
void *leaf_node_value(void *node, uint32_t cell_num);

void initialize_leaf_node(void *node);
/* Binary search: returns the index of `key` if present, otherwise the index
 * where it would have to be inserted to keep the cells sorted. */
uint32_t leaf_node_find_cell(const void *node, uint32_t key);
/* Inserts a cell at position `cell_num`, shifting later cells right by one.
 * The caller checks there is room. */
void leaf_node_insert(void *node, uint32_t cell_num, uint32_t key, const Row *value);

#endif
