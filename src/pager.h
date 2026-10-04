#ifndef PAGER_H
#define PAGER_H

/*
 * The pager is the only module that touches the database file. Everything
 * above it asks for "page N" and gets back a 4KB buffer in memory; the pager
 * decides when bytes actually move between that buffer and disk.
 *
 * For now every page that is touched stays in memory until close. Phase 6
 * replaces this with a fixed-size cache and LRU eviction.
 */

#include <stdint.h>

#define PAGE_SIZE 4096u
#define PAGER_MAX_PAGES 100u

typedef struct {
    int fd;
    uint32_t file_length;
    uint32_t num_pages; /* pages that exist on disk or have been handed out */
    void *pages[PAGER_MAX_PAGES]; /* NULL until a page is first requested */
} Pager;

Pager *pager_open(const char *filename);
void *pager_get_page(Pager *pager, uint32_t page_num);
/* Writes a cached page back to its slot in the file. */
void pager_flush(Pager *pager, uint32_t page_num);
/* Frees every cached page and closes the file; does not flush. */
void pager_close(Pager *pager);

#endif
