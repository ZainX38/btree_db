#ifndef BTREE_H
#define BTREE_H

/*
 * On-disk B+tree node layout and the accessors that read and write it.
 *
 * Every node is one 4KB page. Fields sit at fixed byte offsets and are always
 * read and written with memcpy (see btree.c), never by casting the page to a
 * struct, because some fields are not aligned (e.g. a uint32_t at offset 6).
 *
 * Page 0 is always the root. It starts as a leaf and becomes an internal node
 * the first time it splits.
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

/* Internal header: common header, then num_keys(4) right_child(4). */
#define INTERNAL_NODE_NUM_KEYS_SIZE 4u
#define INTERNAL_NODE_NUM_KEYS_OFFSET COMMON_NODE_HEADER_SIZE
#define INTERNAL_NODE_RIGHT_CHILD_SIZE 4u
#define INTERNAL_NODE_RIGHT_CHILD_OFFSET                                                          \
    (INTERNAL_NODE_NUM_KEYS_OFFSET + INTERNAL_NODE_NUM_KEYS_SIZE)
#define INTERNAL_NODE_HEADER_SIZE                                                                 \
    (COMMON_NODE_HEADER_SIZE + INTERNAL_NODE_NUM_KEYS_SIZE + INTERNAL_NODE_RIGHT_CHILD_SIZE)

/* Internal body: cells of child(4) key(4). Key i is the largest key in child
 * i's subtree; anything larger than every key lives under right_child. */
#define INTERNAL_NODE_CHILD_SIZE 4u
#define INTERNAL_NODE_CHILD_OFFSET 0u
#define INTERNAL_NODE_KEY_SIZE 4u
#define INTERNAL_NODE_KEY_OFFSET (INTERNAL_NODE_CHILD_OFFSET + INTERNAL_NODE_CHILD_SIZE)
#define INTERNAL_NODE_CELL_SIZE (INTERNAL_NODE_CHILD_SIZE + INTERNAL_NODE_KEY_SIZE)
#define INTERNAL_NODE_MAX_KEYS_DEFAULT                                                            \
    ((PAGE_SIZE - INTERNAL_NODE_HEADER_SIZE) / INTERNAL_NODE_CELL_SIZE)
/* Tests build a second binary with a tiny fan-out (see the Makefile), so deep
 * trees and internal splits show up after a few dozen rows. The on-disk
 * layout is the same; nodes just stop accepting keys earlier. */
#ifdef INTERNAL_NODE_MAX_KEYS_OVERRIDE
#define INTERNAL_NODE_MAX_KEYS ((uint32_t)INTERNAL_NODE_MAX_KEYS_OVERRIDE)
#else
#define INTERNAL_NODE_MAX_KEYS INTERNAL_NODE_MAX_KEYS_DEFAULT
#endif

/* Page 0 is always the root, so it can never be anyone's next leaf. That
 * makes 0 a safe "no next leaf" marker. */
#define NO_NEXT_LEAF 0u
/* Marks an internal node that doesn't have a right child yet. */
#define INVALID_PAGE_NUM UINT32_MAX

#define ROOT_PAGE_NUM 0u

typedef struct {
    Pager *pager;
    uint32_t root_page_num;
} Table;

/* With `sync` false the WAL is never fsynced: a killed process loses
 * nothing, because its writes are already in the OS cache, but a power cut
 * can lose recent commits. */
Table *db_open(const char *filename, uint32_t cache_frames, bool sync);
/* Writes every cached page back to disk, then releases the table. */
void db_close(Table *table);

NodeType get_node_type(const void *node);
void set_node_type(void *node, NodeType type);
bool is_node_root(const void *node);
void set_node_root(void *node, bool is_root);
uint32_t node_parent(const void *node);
void set_node_parent(void *node, uint32_t parent);
/* Largest key stored anywhere in the subtree rooted at `page_num`. */
uint32_t get_node_max_key(Pager *pager, uint32_t page_num);

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

uint32_t internal_node_num_keys(const void *node);
void set_internal_node_num_keys(void *node, uint32_t num_keys);
uint32_t internal_node_right_child(const void *node);
void set_internal_node_right_child(void *node, uint32_t page_num);
/* Child `child_num` in 0..num_keys; child num_keys is the right child. */
uint32_t internal_node_child(const void *node, uint32_t child_num);
void set_internal_node_child(void *node, uint32_t child_num, uint32_t page_num);
uint32_t internal_node_key(const void *node, uint32_t key_num);
void set_internal_node_key(void *node, uint32_t key_num, uint32_t key);

void initialize_internal_node(void *node);
/* Binary search: index of the child whose subtree should contain `key`. */
uint32_t internal_node_find_child(const void *node, uint32_t key);

/* Checks every structural rule of the tree (key order, parent pointers, leaf
 * depth, the next_leaf chain). Prints the first problem found, or
 * "Tree is valid.", and returns whether the tree is valid. */
bool validate_tree(Table *table);

/* Prints the subtree rooted at `page_num`, indented by `level`, for the
 * .btree debugging command. */
void print_tree(Pager *pager, uint32_t page_num, uint32_t level);
/* Inserts a row at position `cell_num` of leaf `page_num`, splitting the leaf
 * (and as many ancestors as needed) if it is full. */
void leaf_node_insert(Table *table, uint32_t page_num, uint32_t cell_num, uint32_t key,
                      const Row *value);

#endif
