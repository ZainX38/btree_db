#include "btree.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(LEAF_NODE_HEADER_SIZE == 14, "leaf header layout must stay at 14 bytes");
_Static_assert(LEAF_NODE_MAX_CELLS == 13, "a 4KB leaf holds 13 cells of 297 bytes");
_Static_assert(INTERNAL_NODE_HEADER_SIZE == 14, "internal header layout must stay at 14 bytes");
_Static_assert(INTERNAL_NODE_MAX_KEYS == 510, "a 4KB internal node holds 510 keys");

/* Byte-level helpers. memcpy compiles down to a plain load/store on x86, but
 * unlike a pointer cast it is defined for unaligned offsets. */
static uint32_t read_u32(const void *node, uint32_t offset) {
    uint32_t value;
    memcpy(&value, (const char *)node + offset, sizeof(value));
    return value;
}

static void write_u32(void *node, uint32_t offset, uint32_t value) {
    memcpy((char *)node + offset, &value, sizeof(value));
}

static uint8_t read_u8(const void *node, uint32_t offset) {
    return ((const uint8_t *)node)[offset];
}

static void write_u8(void *node, uint32_t offset, uint8_t value) {
    ((uint8_t *)node)[offset] = value;
}

static uint32_t leaf_node_cell_offset(uint32_t cell_num) {
    return LEAF_NODE_HEADER_SIZE + cell_num * LEAF_NODE_CELL_SIZE;
}

NodeType get_node_type(const void *node) {
    return (NodeType)read_u8(node, NODE_TYPE_OFFSET);
}

void set_node_type(void *node, NodeType type) {
    write_u8(node, NODE_TYPE_OFFSET, (uint8_t)type);
}

bool is_node_root(const void *node) {
    return read_u8(node, IS_ROOT_OFFSET) != 0;
}

void set_node_root(void *node, bool is_root) {
    write_u8(node, IS_ROOT_OFFSET, is_root ? 1 : 0);
}

uint32_t node_parent(const void *node) {
    return read_u32(node, PARENT_POINTER_OFFSET);
}

void set_node_parent(void *node, uint32_t parent) {
    write_u32(node, PARENT_POINTER_OFFSET, parent);
}

uint32_t get_node_max_key(Pager *pager, const void *node) {
    if (get_node_type(node) == NODE_LEAF) {
        return leaf_node_key(node, leaf_node_num_cells(node) - 1);
    }
    /* Keys in an internal node only cover the children to their left; the
     * largest key is always at the bottom of the right-most path. */
    void *right_child = pager_get_page(pager, internal_node_right_child(node));
    return get_node_max_key(pager, right_child);
}

uint32_t leaf_node_num_cells(const void *node) {
    return read_u32(node, LEAF_NODE_NUM_CELLS_OFFSET);
}

void set_leaf_node_num_cells(void *node, uint32_t num_cells) {
    write_u32(node, LEAF_NODE_NUM_CELLS_OFFSET, num_cells);
}

uint32_t leaf_node_next_leaf(const void *node) {
    return read_u32(node, LEAF_NODE_NEXT_LEAF_OFFSET);
}

void set_leaf_node_next_leaf(void *node, uint32_t next_leaf) {
    write_u32(node, LEAF_NODE_NEXT_LEAF_OFFSET, next_leaf);
}

uint32_t leaf_node_key(const void *node, uint32_t cell_num) {
    return read_u32(node, leaf_node_cell_offset(cell_num) + LEAF_NODE_KEY_OFFSET);
}

void set_leaf_node_key(void *node, uint32_t cell_num, uint32_t key) {
    write_u32(node, leaf_node_cell_offset(cell_num) + LEAF_NODE_KEY_OFFSET, key);
}

void *leaf_node_value(void *node, uint32_t cell_num) {
    return (char *)node + leaf_node_cell_offset(cell_num) + LEAF_NODE_VALUE_OFFSET;
}

static uint32_t internal_node_cell_offset(uint32_t cell_num) {
    return INTERNAL_NODE_HEADER_SIZE + cell_num * INTERNAL_NODE_CELL_SIZE;
}

uint32_t internal_node_num_keys(const void *node) {
    return read_u32(node, INTERNAL_NODE_NUM_KEYS_OFFSET);
}

void set_internal_node_num_keys(void *node, uint32_t num_keys) {
    write_u32(node, INTERNAL_NODE_NUM_KEYS_OFFSET, num_keys);
}

uint32_t internal_node_right_child(const void *node) {
    return read_u32(node, INTERNAL_NODE_RIGHT_CHILD_OFFSET);
}

void set_internal_node_right_child(void *node, uint32_t page_num) {
    write_u32(node, INTERNAL_NODE_RIGHT_CHILD_OFFSET, page_num);
}

/* Following a bad child pointer would silently read some unrelated page, so
 * an out-of-range index is treated as a bug and stops the program. */
static void check_child_num(const void *node, uint32_t child_num) {
    if (child_num > internal_node_num_keys(node)) {
        fprintf(stderr, "Child %u out of range (node has %u keys).\n", child_num,
                internal_node_num_keys(node));
        exit(EXIT_FAILURE);
    }
}

uint32_t internal_node_child(const void *node, uint32_t child_num) {
    check_child_num(node, child_num);
    if (child_num == internal_node_num_keys(node)) {
        return internal_node_right_child(node);
    }
    return read_u32(node, internal_node_cell_offset(child_num) + INTERNAL_NODE_CHILD_OFFSET);
}

void set_internal_node_child(void *node, uint32_t child_num, uint32_t page_num) {
    check_child_num(node, child_num);
    if (child_num == internal_node_num_keys(node)) {
        set_internal_node_right_child(node, page_num);
        return;
    }
    write_u32(node, internal_node_cell_offset(child_num) + INTERNAL_NODE_CHILD_OFFSET, page_num);
}

uint32_t internal_node_key(const void *node, uint32_t key_num) {
    return read_u32(node, internal_node_cell_offset(key_num) + INTERNAL_NODE_KEY_OFFSET);
}

void set_internal_node_key(void *node, uint32_t key_num, uint32_t key) {
    write_u32(node, internal_node_cell_offset(key_num) + INTERNAL_NODE_KEY_OFFSET, key);
}

void initialize_internal_node(void *node) {
    memset(node, 0, PAGE_SIZE);
    set_node_type(node, NODE_INTERNAL);
    set_node_root(node, false);
    set_internal_node_num_keys(node, 0);
    /* A zeroed right child would point at page 0 (the root), which would
     * turn any bug into an infinite loop instead of an error. */
    set_internal_node_right_child(node, INVALID_PAGE_NUM);
}

uint32_t internal_node_find_child(const void *node, uint32_t key) {
    /* Find the first key >= `key`: since key i is the max of child i, that
     * child is the only one that can hold `key`. If no key qualifies, the
     * answer is num_keys, i.e. the right child. */
    uint32_t low = 0;
    uint32_t high = internal_node_num_keys(node);
    while (low < high) {
        uint32_t mid = low + (high - low) / 2;
        if (internal_node_key(node, mid) >= key) {
            high = mid;
        } else {
            low = mid + 1;
        }
    }
    return low;
}

void initialize_leaf_node(void *node) {
    memset(node, 0, PAGE_SIZE);
    set_node_type(node, NODE_LEAF);
    set_node_root(node, false);
    set_leaf_node_num_cells(node, 0);
    set_leaf_node_next_leaf(node, NO_NEXT_LEAF);
}

uint32_t leaf_node_find_cell(const void *node, uint32_t key) {
    /* Search the half-open range [low, high). Invariant: every cell before
     * `low` has a smaller key, every cell from `high` on has a key >= `key`. */
    uint32_t low = 0;
    uint32_t high = leaf_node_num_cells(node);
    while (low < high) {
        /* low + (high - low) / 2 can't overflow, unlike (low + high) / 2. */
        uint32_t mid = low + (high - low) / 2;
        uint32_t mid_key = leaf_node_key(node, mid);
        if (mid_key == key) {
            return mid;
        }
        if (mid_key < key) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    return low;
}

static void *leaf_node_cell(void *node, uint32_t cell_num) {
    return (char *)node + leaf_node_cell_offset(cell_num);
}

void leaf_node_insert(void *node, uint32_t cell_num, uint32_t key, const Row *value) {
    uint32_t num_cells = leaf_node_num_cells(node);
    if (cell_num < num_cells) {
        /* memmove, not memcpy: source and destination overlap. */
        memmove(leaf_node_cell(node, cell_num + 1), leaf_node_cell(node, cell_num),
                (size_t)(num_cells - cell_num) * LEAF_NODE_CELL_SIZE);
    }
    set_leaf_node_key(node, cell_num, key);
    serialize_row(value, leaf_node_value(node, cell_num));
    set_leaf_node_num_cells(node, num_cells + 1);
}

static void print_indent(uint32_t level) {
    for (uint32_t i = 0; i < level; i++) {
        printf("  ");
    }
}

void print_tree(Pager *pager, uint32_t page_num, uint32_t level) {
    void *node = pager_get_page(pager, page_num);
    if (get_node_type(node) == NODE_LEAF) {
        uint32_t num_cells = leaf_node_num_cells(node);
        print_indent(level);
        /* Page numbers are printed so splits can be checked against the
         * on-disk layout, not just the key order. */
        printf("- leaf (page %u, size %u)\n", page_num, num_cells);
        for (uint32_t i = 0; i < num_cells; i++) {
            print_indent(level + 1);
            printf("- %u\n", leaf_node_key(node, i));
        }
        return;
    }

    uint32_t num_keys = internal_node_num_keys(node);
    print_indent(level);
    printf("- internal (page %u, size %u)\n", page_num, num_keys);
    for (uint32_t i = 0; i < num_keys; i++) {
        print_tree(pager, internal_node_child(node, i), level + 1);
        print_indent(level + 1);
        printf("- key %u\n", internal_node_key(node, i));
    }
    print_tree(pager, internal_node_right_child(node), level + 1);
}

Table *db_open(const char *filename) {
    Table *table = calloc(1, sizeof(Table));
    if (table == NULL) {
        perror("calloc");
        exit(EXIT_FAILURE);
    }
    table->pager = pager_open(filename);
    table->root_page_num = ROOT_PAGE_NUM;

    if (table->pager->num_pages == 0) {
        /* Brand new file: page 0 becomes an empty leaf that is also the root. */
        void *root = pager_get_page(table->pager, ROOT_PAGE_NUM);
        initialize_leaf_node(root);
        set_node_root(root, true);
    }
    return table;
}

void db_close(Table *table) {
    Pager *pager = table->pager;
    for (uint32_t i = 0; i < pager->num_pages; i++) {
        pager_flush(pager, i);
    }
    pager_close(pager);
    free(table);
}
