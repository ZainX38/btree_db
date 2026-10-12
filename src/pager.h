#ifndef PAGER_H
#define PAGER_H

/*
 * The pager is the only module that touches the database file. Everything
 * above it asks for "page N" and gets back a 4KB buffer in memory; the pager
 * decides when bytes actually move between that buffer and disk.
 *
 * Pages live in a fixed number of frames. When every frame is in use, the
 * least recently used unpinned frame is evicted (written back first only if
 * it is dirty), so the database can be far bigger than the cache.
 *
 * Usage rules, which every caller follows:
 *  - pager_get_page() pins the page. The pointer stays valid until the
 *    matching pager_unpin(); after that the page may be evicted.
 *  - Anyone who changes a page calls pager_mark_dirty() while it's pinned,
 *    so the change gets written back. Clean pages are never written.
 *  - Nothing may still be pinned when the pager is closed.
 */

#include <stdbool.h>
#include <stdint.h>

#define PAGE_SIZE 4096u
#define PAGER_DEFAULT_FRAMES 64u
/* No operation pins more than two pages at once; four leaves headroom. */
#define PAGER_MIN_FRAMES 4u
/* Upper bound for --cache-frames (4GB of cache), mostly to reject typos. */
#define PAGER_MAX_FRAMES (1u << 20)
/* Marks an empty frame. Also caps page numbers, so the file can hold up to
 * UINT32_MAX - 1 pages (about 16TB). */
#define PAGER_NO_PAGE UINT32_MAX

typedef struct {
    uint32_t page_num; /* PAGER_NO_PAGE while the frame is empty */
    uint32_t pin_count;
    bool dirty;
    uint64_t last_used; /* pager clock value at the most recent get_page */
    void *data;
} Frame;

typedef struct {
    int fd;
    uint32_t num_pages; /* pages that exist on disk or have been handed out */
    uint32_t num_frames;
    Frame *frames;
    /* Ticks once per get_page; comparing last_used values gives LRU order
     * without keeping a linked list in sync. */
    uint64_t clock;
    uint64_t hits;
    uint64_t misses;
    uint64_t evictions;
} Pager;

Pager *pager_open(const char *filename, uint32_t num_frames);
/* Returns the page's buffer and pins it. */
void *pager_get_page(Pager *pager, uint32_t page_num);
void pager_unpin(Pager *pager, uint32_t page_num);
/* Records that a pinned page has changed and must be written back. */
void pager_mark_dirty(Pager *pager, uint32_t page_num);
/* Pages are never freed yet, so a new page always goes at the end of the
 * file. The page only counts as used once it is requested with get_page. */
uint32_t pager_get_unused_page_num(const Pager *pager);
/* Writes every dirty page back to the file. */
void pager_flush_all(Pager *pager);
/* Frees the cache and closes the file; does not flush. */
void pager_close(Pager *pager);

#endif
