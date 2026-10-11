#ifndef PAGER_H
#define PAGER_H

/*
 * The pager is the only module that touches the database file. Everything
 * above it asks for "page N" and gets back a 4KB buffer in memory; the pager
 * decides when bytes actually move between that buffer and disk.
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
/* Every page stays in memory until the cache arrives, so this caps the file
 * at 8MB. Deep test trees (small fan-out) need a few hundred pages. */
#define PAGER_MAX_PAGES 2048u

typedef struct {
    void *data; /* NULL until the page is first requested */
    uint32_t pin_count;
    bool dirty;
} CachedPage;

typedef struct {
    int fd;
    uint32_t num_pages; /* pages that exist on disk or have been handed out */
    CachedPage pages[PAGER_MAX_PAGES];
} Pager;

Pager *pager_open(const char *filename);
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
/* Frees every cached page and closes the file; does not flush. */
void pager_close(Pager *pager);

#endif
