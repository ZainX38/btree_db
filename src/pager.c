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

static void *alloc_page(void) {
    void *page = calloc(1, PAGE_SIZE);
    if (page == NULL) {
        die("calloc");
    }
    return page;
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

    Pager *pager = calloc(1, sizeof(Pager));
    if (pager == NULL) {
        die("calloc");
    }
    pager->fd = fd;
    pager->file_length = (uint32_t)st.st_size;
    /* Round up: a partly written last page still counts as a page. */
    pager->num_pages = (pager->file_length + PAGE_SIZE - 1) / PAGE_SIZE;
    return pager;
}

/* Fills a fresh buffer from disk. A short read is fine (the last page may be
 * partial); calloc already zeroed whatever the file doesn't cover. */
static void load_page(Pager *pager, uint32_t page_num, void *page) {
    if (page_num >= (pager->file_length + PAGE_SIZE - 1) / PAGE_SIZE) {
        return; /* page has never been written */
    }
    if (pread(pager->fd, page, PAGE_SIZE, page_offset(page_num)) < 0) {
        die("pread");
    }
}

void *pager_get_page(Pager *pager, uint32_t page_num) {
    if (page_num >= PAGER_MAX_PAGES) {
        fprintf(stderr, "Page %u is out of bounds (max %u).\n", page_num, PAGER_MAX_PAGES);
        exit(EXIT_FAILURE);
    }
    if (pager->pages[page_num] == NULL) {
        void *page = alloc_page();
        load_page(pager, page_num, page);
        pager->pages[page_num] = page;
        if (page_num >= pager->num_pages) {
            pager->num_pages = page_num + 1;
        }
    }
    return pager->pages[page_num];
}

void pager_flush(Pager *pager, uint32_t page_num, uint32_t size) {
    void *page = pager->pages[page_num];
    if (page == NULL) {
        return; /* never loaded, so the copy on disk is already current */
    }
    ssize_t written = pwrite(pager->fd, page, size, page_offset(page_num));
    if (written < 0 || (size_t)written != size) {
        die("pwrite");
    }
}

void pager_close(Pager *pager) {
    for (uint32_t i = 0; i < PAGER_MAX_PAGES; i++) {
        free(pager->pages[i]);
    }
    if (close(pager->fd) < 0) {
        die("close");
    }
    free(pager);
}
