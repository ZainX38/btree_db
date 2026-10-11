#include "pager.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* Read and write owner only: the database may hold personal data. */
#define DB_FILE_MODE (S_IRUSR | S_IWUSR)

/* I/O errors here mean the database can't be trusted any more, so we stop
 * instead of trying to carry on with half-written pages. */
static void die(const char *what) {
    perror(what);
    exit(EXIT_FAILURE);
}

/* Pin bookkeeping mistakes are bugs in the caller, not runtime conditions,
 * and carrying on would risk using an evicted buffer. */
static void pin_bug(const char *problem, uint32_t page_num) {
    fprintf(stderr, "Pager bug: %s (page %u).\n", problem, page_num);
    abort();
}

static off_t page_offset(uint32_t page_num) {
    return (off_t)page_num * PAGE_SIZE;
}

Pager *pager_open(const char *filename) {
    int fd = open(filename, O_RDWR | O_CREAT, DB_FILE_MODE);
    if (fd < 0) {
        die("open");
    }
    struct stat st;
    if (fstat(fd, &st) < 0) {
        die("fstat");
    }
    if (st.st_size > (off_t)PAGER_MAX_PAGES * PAGE_SIZE) {
        fprintf(stderr, "Database file is larger than %u pages.\n", PAGER_MAX_PAGES);
        exit(EXIT_FAILURE);
    }
    /* We only ever write whole pages, so anything else means a torn or
     * foreign file. */
    if (st.st_size % PAGE_SIZE != 0) {
        fprintf(stderr, "Database file is corrupt: not a whole number of pages.\n");
        exit(EXIT_FAILURE);
    }

    Pager *pager = calloc(1, sizeof(Pager));
    if (pager == NULL) {
        die("calloc");
    }
    pager->fd = fd;
    pager->num_pages = (uint32_t)(st.st_size / PAGE_SIZE);
    return pager;
}

/* Fills a buffer from disk. A page past the end of the file has never been
 * written, so it reads as zeros; anything between 0 and a full page is a
 * torn write. */
static void load_page(Pager *pager, uint32_t page_num, void *page) {
    ssize_t got = pread(pager->fd, page, PAGE_SIZE, page_offset(page_num));
    if (got != 0 && got != (ssize_t)PAGE_SIZE) {
        die("pread");
    }
}

void *pager_get_page(Pager *pager, uint32_t page_num) {
    if (page_num >= PAGER_MAX_PAGES) {
        fprintf(stderr, "Page %u is out of bounds (max %u).\n", page_num, PAGER_MAX_PAGES);
        exit(EXIT_FAILURE);
    }
    CachedPage *entry = &pager->pages[page_num];
    if (entry->data == NULL) {
        entry->data = calloc(1, PAGE_SIZE);
        if (entry->data == NULL) {
            die("calloc");
        }
        load_page(pager, page_num, entry->data);
        if (page_num >= pager->num_pages) {
            pager->num_pages = page_num + 1;
        }
    }
    entry->pin_count++;
    return entry->data;
}

void pager_unpin(Pager *pager, uint32_t page_num) {
    CachedPage *entry = &pager->pages[page_num];
    if (entry->pin_count == 0) {
        pin_bug("unpin of a page that isn't pinned", page_num);
    }
    entry->pin_count--;
}

void pager_mark_dirty(Pager *pager, uint32_t page_num) {
    CachedPage *entry = &pager->pages[page_num];
    /* Writing to an unpinned page means the caller kept a stale pointer. */
    if (entry->pin_count == 0) {
        pin_bug("page marked dirty while not pinned", page_num);
    }
    entry->dirty = true;
}

uint32_t pager_get_unused_page_num(const Pager *pager) {
    return pager->num_pages;
}

static void write_page(Pager *pager, uint32_t page_num, const void *data) {
    ssize_t written = pwrite(pager->fd, data, PAGE_SIZE, page_offset(page_num));
    if (written != (ssize_t)PAGE_SIZE) {
        die("pwrite");
    }
}

void pager_flush_all(Pager *pager) {
    for (uint32_t i = 0; i < pager->num_pages; i++) {
        CachedPage *entry = &pager->pages[i];
        if (entry->data != NULL && entry->dirty) {
            write_page(pager, i, entry->data);
            entry->dirty = false;
        }
    }
}

void pager_close(Pager *pager) {
    for (uint32_t i = 0; i < PAGER_MAX_PAGES; i++) {
        if (pager->pages[i].pin_count != 0) {
            pin_bug("page still pinned at close", i);
        }
        free(pager->pages[i].data);
    }
    if (close(pager->fd) < 0) {
        die("close");
    }
    free(pager);
}
