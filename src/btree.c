#include "btree.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(LEAF_NODE_HEADER_SIZE == 14, "leaf header layout must stay at 14 bytes");
_Static_assert(LEAF_NODE_MAX_CELLS == 13, "a 4KB leaf holds 13 cells of 297 bytes");
_Static_assert(INTERNAL_NODE_HEADER_SIZE == 14, "internal header layout must stay at 14 bytes");
_Static_assert(INTERNAL_NODE_MAX_KEYS == 510, "a 4KB internal node holds 510 keys");

/* A split shares the 13 existing cells plus the new one between two leaves,
 * as evenly as possible (7 and 7). */
#define LEAF_NODE_RIGHT_SPLIT_COUNT ((LEAF_NODE_MAX_CELLS + 1) / 2)
#define LEAF_NODE_LEFT_SPLIT_COUNT ((LEAF_NODE_MAX_CELLS + 1) - LEAF_NODE_RIGHT_SPLIT_COUNT)

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

static void write_leaf_cell(void *node, uint32_t cell_num, uint32_t key, const Row *value) {
    set_leaf_node_key(node, cell_num, key);
    serialize_row(value, leaf_node_value(node, cell_num));
}

/* Inserts into a leaf that is known to have room. */
static void leaf_node_insert_cell(void *node, uint32_t cell_num, uint32_t key,
                                  const Row *value) {
    uint32_t num_cells = leaf_node_num_cells(node);
    if (cell_num < num_cells) {
        /* memmove, not memcpy: source and destination overlap. */
        memmove(leaf_node_cell(node, cell_num + 1), leaf_node_cell(node, cell_num),
                (size_t)(num_cells - cell_num) * LEAF_NODE_CELL_SIZE);
    }
    write_leaf_cell(node, cell_num, key, value);
    set_leaf_node_num_cells(node, num_cells + 1);
}

/* Page 0 has to stay the root, so the root can't simply get a new parent.
 * Instead its contents (the left half after a split) move to a fresh page,
 * and page 0 is rewritten as an internal node pointing at both halves. */
static void create_new_root(Table *table, uint32_t right_child_page_num) {
    Pager *pager = table->pager;
    void *root = pager_get_page(pager, table->root_page_num);
    void *right_child = pager_get_page(pager, right_child_page_num);
    uint32_t left_child_page_num = pager_get_unused_page_num(pager);
    void *left_child = pager_get_page(pager, left_child_page_num);

    memcpy(left_child, root, PAGE_SIZE);
    set_node_root(left_child, false);

    initialize_internal_node(root);
    set_node_root(root, true);
    set_internal_node_num_keys(root, 1);
    set_internal_node_child(root, 0, left_child_page_num);
    set_internal_node_key(root, 0, get_node_max_key(pager, left_child));
    set_internal_node_right_child(root, right_child_page_num);
    set_node_parent(left_child, table->root_page_num);
    set_node_parent(right_child, table->root_page_num);
}

/* After a child splits, its largest key gets smaller; the parent's key for it
 * has to follow. A child that is the right child has no key to update. */
static void update_internal_node_key(void *node, uint32_t old_key, uint32_t new_key) {
    uint32_t key_num = internal_node_find_child(node, old_key);
    if (key_num < internal_node_num_keys(node)) {
        set_internal_node_key(node, key_num, new_key);
    }
}

/* Adds `child_page_num` (the new right half of a split) to its parent. The
 * caller has already checked the parent has room. */
static void internal_node_insert(Table *table, uint32_t parent_page_num,
                                 uint32_t child_page_num) {
    Pager *pager = table->pager;
    void *parent = pager_get_page(pager, parent_page_num);
    void *child = pager_get_page(pager, child_page_num);
    uint32_t child_max_key = get_node_max_key(pager, child);
    uint32_t index = internal_node_find_child(parent, child_max_key);
    uint32_t original_num_keys = internal_node_num_keys(parent);

    uint32_t right_child_page_num = internal_node_right_child(parent);
    void *right_child = pager_get_page(pager, right_child_page_num);

    if (child_max_key > get_node_max_key(pager, right_child)) {
        /* The new child holds the largest keys, so it becomes the right
         * child and the old right child moves into the last cell. */
        set_internal_node_num_keys(parent, original_num_keys + 1);
        set_internal_node_child(parent, original_num_keys, right_child_page_num);
        set_internal_node_key(parent, original_num_keys, get_node_max_key(pager, right_child));
        set_internal_node_right_child(parent, child_page_num);
        return;
    }

    /* Otherwise make room at `index` by shifting later cells right by one. */
    char *cells = (char *)parent + INTERNAL_NODE_HEADER_SIZE;
    memmove(cells + (index + 1) * INTERNAL_NODE_CELL_SIZE, cells + index * INTERNAL_NODE_CELL_SIZE,
            (size_t)(original_num_keys - index) * INTERNAL_NODE_CELL_SIZE);
    set_internal_node_num_keys(parent, original_num_keys + 1);
    set_internal_node_child(parent, index, child_page_num);
    set_internal_node_key(parent, index, child_max_key);
}

/* Moves the 13 old cells plus the new one into the old leaf (left half) and a
 * new leaf (right half), keeping them sorted. */
static void leaf_node_split_and_insert(Table *table, uint32_t old_page_num, uint32_t cell_num,
                                       uint32_t key, const Row *value) {
    Pager *pager = table->pager;
    void *old_node = pager_get_page(pager, old_page_num);
    /* Remember the max before the cells move, so the parent's key for the old
     * leaf can be found afterwards. */
    uint32_t old_max_key = get_node_max_key(pager, old_node);
    uint32_t new_page_num = pager_get_unused_page_num(pager);
    void *new_node = pager_get_page(pager, new_page_num);
    initialize_leaf_node(new_node);
    set_node_parent(new_node, node_parent(old_node));
    /* The new leaf comes straight after the old one in key order. */
    set_leaf_node_next_leaf(new_node, leaf_node_next_leaf(old_node));
    set_leaf_node_next_leaf(old_node, new_page_num);

    /* Walk the 14 positions from the top down. Going downwards means that
     * when an old cell shifts up by one inside the old leaf, the cell it
     * lands on has already been moved out of the way. */
    for (uint32_t pos = LEAF_NODE_MAX_CELLS + 1; pos-- > 0;) {
        bool goes_right = pos >= LEAF_NODE_LEFT_SPLIT_COUNT;
        void *dest_node = goes_right ? new_node : old_node;
        uint32_t dest_cell = goes_right ? pos - LEAF_NODE_LEFT_SPLIT_COUNT : pos;

        if (pos == cell_num) {
            write_leaf_cell(dest_node, dest_cell, key, value);
            continue;
        }
        /* Old cells after the insert point move up one position. */
        uint32_t src_cell = pos > cell_num ? pos - 1 : pos;
        /* memmove: for cells that stay put, source and destination are the
         * same bytes, which memcpy doesn't allow (ASan reports it). */
        memmove(leaf_node_cell(dest_node, dest_cell), leaf_node_cell(old_node, src_cell),
                LEAF_NODE_CELL_SIZE);
    }
    set_leaf_node_num_cells(old_node, LEAF_NODE_LEFT_SPLIT_COUNT);
    set_leaf_node_num_cells(new_node, LEAF_NODE_RIGHT_SPLIT_COUNT);

    if (is_node_root(old_node)) {
        create_new_root(table, new_page_num);
        return;
    }
    uint32_t parent_page_num = node_parent(old_node);
    void *parent = pager_get_page(pager, parent_page_num);
    update_internal_node_key(parent, old_max_key, get_node_max_key(pager, old_node));
    internal_node_insert(table, parent_page_num, new_page_num);
}

bool leaf_node_insert(Table *table, uint32_t page_num, uint32_t cell_num, uint32_t key,
                      const Row *value) {
    Pager *pager = table->pager;
    void *node = pager_get_page(pager, page_num);
    if (leaf_node_num_cells(node) < LEAF_NODE_MAX_CELLS) {
        leaf_node_insert_cell(node, cell_num, key, value);
        return true;
    }
    /* Check before splitting: once the cells have moved there is no clean
     * way back if the parent turns out to be full. */
    if (!is_node_root(node)) {
        void *parent = pager_get_page(pager, node_parent(node));
        if (internal_node_num_keys(parent) >= INTERNAL_NODE_MAX_KEYS) {
            return false;
        }
    }
    leaf_node_split_and_insert(table, page_num, cell_num, key, value);
    return true;
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
