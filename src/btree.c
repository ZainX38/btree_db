#include "btree.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(LEAF_NODE_HEADER_SIZE == 14, "leaf header layout must stay at 14 bytes");
_Static_assert(LEAF_NODE_MAX_CELLS == 13, "a 4KB leaf holds 13 cells of 297 bytes");
_Static_assert(INTERNAL_NODE_HEADER_SIZE == 14, "internal header layout must stay at 14 bytes");
_Static_assert(INTERNAL_NODE_MAX_KEYS_DEFAULT == 510, "a 4KB internal node holds 510 keys");
_Static_assert(INTERNAL_NODE_MAX_KEYS >= 2 &&
                   INTERNAL_NODE_MAX_KEYS <= INTERNAL_NODE_MAX_KEYS_DEFAULT,
               "fan-out override must leave both halves of a split non-empty and fit in a page");

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

/*
 * Everything below works on page numbers and follows the pager's pin rules:
 * a page is pinned only while its bytes are being read or written, and is
 * unpinned before calling anything that may walk to other pages (especially
 * before recursing up to a parent), so an insert never holds more than a
 * handful of pins however deep the tree is.
 */

/* Small helpers for internal nodes that pin, read one field, and unpin. */
static uint32_t read_num_keys(Pager *pager, uint32_t page_num) {
    void *node = pager_get_page(pager, page_num);
    uint32_t num_keys = internal_node_num_keys(node);
    pager_unpin(pager, page_num);
    return num_keys;
}

static uint32_t read_child(Pager *pager, uint32_t page_num, uint32_t child_num) {
    void *node = pager_get_page(pager, page_num);
    uint32_t child = internal_node_child(node, child_num);
    pager_unpin(pager, page_num);
    return child;
}

static uint32_t read_key(Pager *pager, uint32_t page_num, uint32_t key_num) {
    void *node = pager_get_page(pager, page_num);
    uint32_t key = internal_node_key(node, key_num);
    pager_unpin(pager, page_num);
    return key;
}

static void set_parent_of(Pager *pager, uint32_t page_num, uint32_t parent) {
    void *node = pager_get_page(pager, page_num);
    set_node_parent(node, parent);
    pager_mark_dirty(pager, page_num);
    pager_unpin(pager, page_num);
}

uint32_t get_node_max_key(Pager *pager, uint32_t page_num) {
    /* Keys in an internal node only cover the children to their left; the
     * largest key is always at the bottom of the right-most path. Walk down
     * one pin at a time rather than recursing with every level pinned. */
    for (;;) {
        void *node = pager_get_page(pager, page_num);
        if (get_node_type(node) == NODE_LEAF) {
            uint32_t max_key = leaf_node_key(node, leaf_node_num_cells(node) - 1);
            pager_unpin(pager, page_num);
            return max_key;
        }
        uint32_t right_child = internal_node_right_child(node);
        pager_unpin(pager, page_num);
        page_num = right_child;
    }
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
    uint32_t root_page_num = table->root_page_num;
    uint32_t left_child_page_num = pager_get_unused_page_num(pager);
    void *root = pager_get_page(pager, root_page_num);
    void *left_child = pager_get_page(pager, left_child_page_num);

    memcpy(left_child, root, PAGE_SIZE);
    set_node_root(left_child, false);
    set_node_parent(left_child, root_page_num);
    bool left_is_internal = get_node_type(left_child) == NODE_INTERNAL;
    pager_mark_dirty(pager, left_child_page_num);
    pager_unpin(pager, left_child_page_num);
    pager_unpin(pager, root_page_num);

    /* Read the left half's max before page 0 is overwritten below. */
    uint32_t left_max_key = get_node_max_key(pager, left_child_page_num);
    root = pager_get_page(pager, root_page_num);
    initialize_internal_node(root);
    set_node_root(root, true);
    set_internal_node_num_keys(root, 1);
    set_internal_node_child(root, 0, left_child_page_num);
    set_internal_node_key(root, 0, left_max_key);
    set_internal_node_right_child(root, right_child_page_num);
    pager_mark_dirty(pager, root_page_num);
    pager_unpin(pager, root_page_num);

    set_parent_of(pager, right_child_page_num, root_page_num);
    /* The old root's children still name page 0 as their parent, but their
     * parent is now the copy. */
    if (left_is_internal) {
        uint32_t num_keys = read_num_keys(pager, left_child_page_num);
        for (uint32_t i = 0; i <= num_keys; i++) {
            set_parent_of(pager, read_child(pager, left_child_page_num, i), left_child_page_num);
        }
    }
}

/* After a child splits, its largest key gets smaller; the parent's key for it
 * has to follow. A child that is the right child has no key to update. */
static void update_internal_node_key(Pager *pager, uint32_t page_num, uint32_t old_key,
                                     uint32_t new_key) {
    void *node = pager_get_page(pager, page_num);
    uint32_t key_num = internal_node_find_child(node, old_key);
    if (key_num < internal_node_num_keys(node)) {
        set_internal_node_key(node, key_num, new_key);
        pager_mark_dirty(pager, page_num);
    }
    pager_unpin(pager, page_num);
}

/* One child of an internal node, with the largest key in its subtree. */
typedef struct {
    uint32_t page_num;
    uint32_t max_key;
} ChildEntry;

/* A full node's MAX_KEYS + 1 children plus the new one are shared between
 * the two halves of a split, the left half taking the extra one if odd. */
#define INTERNAL_SPLIT_ENTRIES (INTERNAL_NODE_MAX_KEYS + 2)
#define INTERNAL_NODE_LEFT_SPLIT_COUNT ((INTERNAL_SPLIT_ENTRIES + 1) / 2)

static void internal_node_insert(Table *table, uint32_t parent_page_num,
                                 uint32_t child_page_num);

/* Lists the children of `page_num` plus `new_child_page_num`, in key order. */
static uint32_t collect_children(Pager *pager, uint32_t page_num, uint32_t new_child_page_num,
                                 ChildEntry *entries) {
    ChildEntry new_entry = {
        .page_num = new_child_page_num,
        .max_key = get_node_max_key(pager, new_child_page_num),
    };
    uint32_t num_keys = read_num_keys(pager, page_num);
    uint32_t count = 0;
    bool placed = false;
    for (uint32_t i = 0; i <= num_keys; i++) {
        uint32_t child_page_num = read_child(pager, page_num, i);
        /* The right child has no key of its own, so ask the subtree. */
        uint32_t max_key = i < num_keys ? read_key(pager, page_num, i)
                                        : get_node_max_key(pager, child_page_num);
        if (!placed && new_entry.max_key < max_key) {
            entries[count++] = new_entry;
            placed = true;
        }
        entries[count++] = (ChildEntry){.page_num = child_page_num, .max_key = max_key};
    }
    if (!placed) {
        entries[count++] = new_entry;
    }
    return count;
}

/* Rewrites node `page_num` to hold `entries`: every child but the last gets a
 * cell keyed by its max key, and the last one becomes the right child. The
 * node keeps its root flag and parent, and every child is told its new
 * parent. */
static void write_internal_node(Pager *pager, uint32_t page_num, const ChildEntry *entries,
                                uint32_t count) {
    void *node = pager_get_page(pager, page_num);
    bool is_root = is_node_root(node);
    uint32_t parent = node_parent(node);
    initialize_internal_node(node);
    set_node_root(node, is_root);
    set_node_parent(node, parent);

    set_internal_node_num_keys(node, count - 1);
    for (uint32_t i = 0; i + 1 < count; i++) {
        set_internal_node_child(node, i, entries[i].page_num);
        set_internal_node_key(node, i, entries[i].max_key);
    }
    set_internal_node_right_child(node, entries[count - 1].page_num);
    pager_mark_dirty(pager, page_num);
    pager_unpin(pager, page_num);

    for (uint32_t i = 0; i < count; i++) {
        set_parent_of(pager, entries[i].page_num, page_num);
    }
}

/* Splits a full internal node while adding `child_page_num` to it. Like a
 * leaf split, the old node keeps the left half, a new node takes the right
 * half, and the new node is then added to the parent (which may split too). */
static void internal_node_split_and_insert(Table *table, uint32_t old_page_num,
                                           uint32_t child_page_num) {
    Pager *pager = table->pager;
    uint32_t old_max_key = get_node_max_key(pager, old_page_num);
    ChildEntry entries[INTERNAL_SPLIT_ENTRIES];
    uint32_t count = collect_children(pager, old_page_num, child_page_num, entries);

    void *old_node = pager_get_page(pager, old_page_num);
    bool old_is_root = is_node_root(old_node);
    uint32_t parent_page_num = node_parent(old_node);
    pager_unpin(pager, old_page_num);

    uint32_t new_page_num = pager_get_unused_page_num(pager);
    void *new_node = pager_get_page(pager, new_page_num);
    initialize_internal_node(new_node);
    set_node_parent(new_node, parent_page_num);
    pager_mark_dirty(pager, new_page_num);
    pager_unpin(pager, new_page_num);

    write_internal_node(pager, old_page_num, entries, INTERNAL_NODE_LEFT_SPLIT_COUNT);
    write_internal_node(pager, new_page_num, entries + INTERNAL_NODE_LEFT_SPLIT_COUNT,
                        count - INTERNAL_NODE_LEFT_SPLIT_COUNT);

    if (old_is_root) {
        create_new_root(table, new_page_num);
        return;
    }
    update_internal_node_key(pager, parent_page_num, old_max_key,
                             get_node_max_key(pager, old_page_num));
    internal_node_insert(table, parent_page_num, new_page_num);
}

/* Adds `child_page_num` (the new right half of a split) to its parent,
 * splitting the parent first if it is full. */
static void internal_node_insert(Table *table, uint32_t parent_page_num,
                                 uint32_t child_page_num) {
    Pager *pager = table->pager;
    uint32_t num_keys = read_num_keys(pager, parent_page_num);
    if (num_keys >= INTERNAL_NODE_MAX_KEYS) {
        internal_node_split_and_insert(table, parent_page_num, child_page_num);
        return;
    }
    /* Work out the max keys first: that walks other pages, which shouldn't
     * happen while the parent is pinned and half-modified. */
    uint32_t child_max_key = get_node_max_key(pager, child_page_num);
    uint32_t right_child_page_num = read_child(pager, parent_page_num, num_keys);
    uint32_t right_max_key = get_node_max_key(pager, right_child_page_num);

    void *parent = pager_get_page(pager, parent_page_num);
    if (child_max_key > right_max_key) {
        /* The new child holds the largest keys, so it becomes the right
         * child and the old right child moves into the last cell. */
        set_internal_node_num_keys(parent, num_keys + 1);
        set_internal_node_child(parent, num_keys, right_child_page_num);
        set_internal_node_key(parent, num_keys, right_max_key);
        set_internal_node_right_child(parent, child_page_num);
    } else {
        /* Otherwise make room at `index` by shifting later cells right. */
        uint32_t index = internal_node_find_child(parent, child_max_key);
        char *cells = (char *)parent + INTERNAL_NODE_HEADER_SIZE;
        memmove(cells + (index + 1) * INTERNAL_NODE_CELL_SIZE,
                cells + index * INTERNAL_NODE_CELL_SIZE,
                (size_t)(num_keys - index) * INTERNAL_NODE_CELL_SIZE);
        set_internal_node_num_keys(parent, num_keys + 1);
        set_internal_node_child(parent, index, child_page_num);
        set_internal_node_key(parent, index, child_max_key);
    }
    pager_mark_dirty(pager, parent_page_num);
    pager_unpin(pager, parent_page_num);
}

/* Moves the 13 old cells plus the new one into the old leaf (left half) and a
 * new leaf (right half), keeping them sorted. */
static void leaf_node_split_and_insert(Table *table, uint32_t old_page_num, uint32_t cell_num,
                                       uint32_t key, const Row *value) {
    Pager *pager = table->pager;
    /* Remember the max before the cells move, so the parent's key for the old
     * leaf can be found afterwards. */
    uint32_t old_max_key = get_node_max_key(pager, old_page_num);
    uint32_t new_page_num = pager_get_unused_page_num(pager);
    void *old_node = pager_get_page(pager, old_page_num);
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

    bool old_is_root = is_node_root(old_node);
    uint32_t parent_page_num = node_parent(old_node);
    pager_mark_dirty(pager, old_page_num);
    pager_mark_dirty(pager, new_page_num);
    pager_unpin(pager, old_page_num);
    pager_unpin(pager, new_page_num);

    if (old_is_root) {
        create_new_root(table, new_page_num);
        return;
    }
    update_internal_node_key(pager, parent_page_num, old_max_key,
                             get_node_max_key(pager, old_page_num));
    internal_node_insert(table, parent_page_num, new_page_num);
}

void leaf_node_insert(Table *table, uint32_t page_num, uint32_t cell_num, uint32_t key,
                      const Row *value) {
    Pager *pager = table->pager;
    void *node = pager_get_page(pager, page_num);
    if (leaf_node_num_cells(node) < LEAF_NODE_MAX_CELLS) {
        leaf_node_insert_cell(node, cell_num, key, value);
        pager_mark_dirty(pager, page_num);
        pager_unpin(pager, page_num);
        return;
    }
    pager_unpin(pager, page_num);
    leaf_node_split_and_insert(table, page_num, cell_num, key, value);
}

/* State carried through the validation walk. Leaves are visited left to
 * right, so each one can be checked against the previous leaf's next_leaf. */
typedef struct {
    Pager *pager;
    bool seen_leaf;
    uint32_t leaf_depth;
    uint32_t prev_leaf;
} TreeCheck;

/* Key bounds use int64_t so "no lower bound" (-1) is outside every uint32_t. */
#define NO_LOWER_BOUND (-1)

static bool check_failed(uint32_t page_num, const char *problem) {
    printf("Invalid tree: page %u: %s.\n", page_num, problem);
    return false;
}

static bool check_node(TreeCheck *check, uint32_t page_num, uint32_t parent, uint32_t depth,
                       int64_t low, int64_t high);

/* Every key in a subtree must lie in (low, high]. */
static bool key_in_range(uint32_t key, int64_t low, int64_t high) {
    return (int64_t)key > low && (int64_t)key <= high;
}

/* Checks a pinned leaf's own cells; returns the problem, or NULL if none. */
static const char *leaf_cells_problem(const void *node, bool is_root, int64_t low,
                                      int64_t high) {
    uint32_t num_cells = leaf_node_num_cells(node);
    if (num_cells > LEAF_NODE_MAX_CELLS) {
        return "leaf has too many cells";
    }
    if (num_cells == 0 && !is_root) {
        return "non-root leaf is empty";
    }
    int64_t prev = low;
    for (uint32_t i = 0; i < num_cells; i++) {
        uint32_t key = leaf_node_key(node, i);
        if ((int64_t)key <= prev || !key_in_range(key, low, high)) {
            return "keys out of order";
        }
        prev = key;
    }
    return NULL;
}

static uint32_t read_next_leaf(Pager *pager, uint32_t page_num) {
    void *node = pager_get_page(pager, page_num);
    uint32_t next = leaf_node_next_leaf(node);
    pager_unpin(pager, page_num);
    return next;
}

static bool check_leaf(TreeCheck *check, uint32_t page_num, uint32_t depth, int64_t low,
                       int64_t high) {
    void *node = pager_get_page(check->pager, page_num);
    const char *problem = leaf_cells_problem(node, page_num == ROOT_PAGE_NUM, low, high);
    pager_unpin(check->pager, page_num);
    if (problem != NULL) {
        return check_failed(page_num, problem);
    }

    /* A B+tree is balanced only if every leaf is at the same depth. */
    if (check->seen_leaf && depth != check->leaf_depth) {
        return check_failed(page_num, "leaves at different depths");
    }
    if (check->seen_leaf && read_next_leaf(check->pager, check->prev_leaf) != page_num) {
        return check_failed(check->prev_leaf, "next_leaf does not point at the next leaf");
    }
    check->seen_leaf = true;
    check->leaf_depth = depth;
    check->prev_leaf = page_num;
    return true;
}

static bool check_internal(TreeCheck *check, uint32_t page_num, uint32_t depth, int64_t low,
                           int64_t high) {
    Pager *pager = check->pager;
    uint32_t num_keys = read_num_keys(pager, page_num);
    if (num_keys == 0 || num_keys > INTERNAL_NODE_MAX_KEYS) {
        return check_failed(page_num, "internal node has a bad number of keys");
    }
    int64_t prev = low;
    for (uint32_t i = 0; i <= num_keys; i++) {
        bool is_right_child = i == num_keys;
        /* The right child is bounded by this node's own upper bound. */
        int64_t child_high = is_right_child ? high : read_key(pager, page_num, i);
        if (!is_right_child && (child_high <= prev || child_high > high)) {
            return check_failed(page_num, "keys out of order");
        }
        uint32_t child_page_num = read_child(pager, page_num, i);
        if (!check_node(check, child_page_num, page_num, depth + 1, prev, child_high)) {
            return false;
        }
        if (!is_right_child && get_node_max_key(pager, child_page_num) != child_high) {
            return check_failed(page_num, "key does not match its child's largest key");
        }
        prev = child_high;
    }
    return true;
}

static bool check_node(TreeCheck *check, uint32_t page_num, uint32_t parent, uint32_t depth,
                       int64_t low, int64_t high) {
    void *node = pager_get_page(check->pager, page_num);
    bool is_root = is_node_root(node);
    uint32_t stored_parent = node_parent(node);
    NodeType type = get_node_type(node);
    pager_unpin(check->pager, page_num);

    bool should_be_root = page_num == ROOT_PAGE_NUM;
    if (is_root != should_be_root) {
        return check_failed(page_num, "root flag is wrong");
    }
    /* The root's parent field is never read, so only children are checked. */
    if (!should_be_root && stored_parent != parent) {
        return check_failed(page_num, "parent pointer is wrong");
    }
    if (type == NODE_LEAF) {
        return check_leaf(check, page_num, depth, low, high);
    }
    return check_internal(check, page_num, depth, low, high);
}

bool validate_tree(Table *table) {
    TreeCheck check = {.pager = table->pager};
    if (!check_node(&check, table->root_page_num, ROOT_PAGE_NUM, 0, NO_LOWER_BOUND,
                    UINT32_MAX)) {
        return false;
    }
    if (read_next_leaf(table->pager, check.prev_leaf) != NO_NEXT_LEAF) {
        return check_failed(check.prev_leaf, "last leaf has a next_leaf");
    }
    printf("Tree is valid.\n");
    return true;
}

static void print_indent(uint32_t level) {
    for (uint32_t i = 0; i < level; i++) {
        printf("  ");
    }
}

static void print_leaf(Pager *pager, uint32_t page_num, uint32_t level) {
    void *node = pager_get_page(pager, page_num);
    uint32_t num_cells = leaf_node_num_cells(node);
    print_indent(level);
    /* Page numbers are printed so splits can be checked against the on-disk
     * layout, not just the key order. */
    printf("- leaf (page %u, size %u)\n", page_num, num_cells);
    for (uint32_t i = 0; i < num_cells; i++) {
        print_indent(level + 1);
        printf("- %u\n", leaf_node_key(node, i));
    }
    pager_unpin(pager, page_num);
}

void print_tree(Pager *pager, uint32_t page_num, uint32_t level) {
    void *node = pager_get_page(pager, page_num);
    NodeType type = get_node_type(node);
    pager_unpin(pager, page_num);
    if (type == NODE_LEAF) {
        print_leaf(pager, page_num, level);
        return;
    }

    uint32_t num_keys = read_num_keys(pager, page_num);
    print_indent(level);
    printf("- internal (page %u, size %u)\n", page_num, num_keys);
    for (uint32_t i = 0; i < num_keys; i++) {
        print_tree(pager, read_child(pager, page_num, i), level + 1);
        print_indent(level + 1);
        printf("- key %u\n", read_key(pager, page_num, i));
    }
    print_tree(pager, read_child(pager, page_num, num_keys), level + 1);
}

Table *db_open(const char *filename, uint32_t cache_frames) {
    Table *table = calloc(1, sizeof(Table));
    if (table == NULL) {
        perror("calloc");
        exit(EXIT_FAILURE);
    }
    table->pager = pager_open(filename, cache_frames);
    table->root_page_num = ROOT_PAGE_NUM;

    if (table->pager->num_pages == 0) {
        /* Brand new file: page 0 becomes an empty leaf that is also the root. */
        void *root = pager_get_page(table->pager, ROOT_PAGE_NUM);
        initialize_leaf_node(root);
        set_node_root(root, true);
        pager_mark_dirty(table->pager, ROOT_PAGE_NUM);
        pager_unpin(table->pager, ROOT_PAGE_NUM);
    }
    return table;
}

void db_close(Table *table) {
    pager_flush_all(table->pager);
    pager_close(table->pager);
    free(table);
}
