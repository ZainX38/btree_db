#include "pager.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static void *xcalloc(size_t count, size_t size) {
    void *p = calloc(count, size);
    if (p == NULL) {
        die("calloc");
    }
    return p;
}

Pager *pager_open(const char *filename, uint32_t num_frames) {
    if (num_frames < PAGER_MIN_FRAMES || num_frames > PAGER_MAX_FRAMES) {
        fprintf(stderr, "Cache must have between %u and %u frames.\n", PAGER_MIN_FRAMES,
                PAGER_MAX_FRAMES);
        exit(EXIT_FAILURE);
    }
    int fd = open(filename, O_RDWR | O_CREAT, DB_FILE_MODE);
    if (fd < 0) {
        die("open");
    }
    struct stat st;
    if (fstat(fd, &st) < 0) {
        die("fstat");
    }
    /* We only ever write whole pages, so anything else means a torn or
     * foreign file. */
    if (st.st_size % PAGE_SIZE != 0) {
        fprintf(stderr, "Database file is corrupt: not a whole number of pages.\n");
        exit(EXIT_FAILURE);
    }
    if (st.st_size / PAGE_SIZE >= PAGER_NO_PAGE) {
        fprintf(stderr, "Database file has too many pages.\n");
        exit(EXIT_FAILURE);
    }

    Pager *pager = xcalloc(1, sizeof(Pager));
    pager->fd = fd;
    pager->num_pages = (uint32_t)(st.st_size / PAGE_SIZE);
    pager->num_frames = num_frames;
    pager->frames = xcalloc(num_frames, sizeof(Frame));
    for (uint32_t i = 0; i < num_frames; i++) {
        pager->frames[i].page_num = PAGER_NO_PAGE;
        pager->frames[i].data = xcalloc(1, PAGE_SIZE);
    }
    return pager;
}

/* A linear scan: with tens of frames it is as fast as a hash table and
 * there's nothing extra to keep consistent. */
static Frame *find_frame(Pager *pager, uint32_t page_num) {
    for (uint32_t i = 0; i < pager->num_frames; i++) {
        if (pager->frames[i].page_num == page_num) {
            return &pager->frames[i];
        }
    }
    return NULL;
}

static void write_page(Pager *pager, uint32_t page_num, const void *data) {
    ssize_t written = pwrite(pager->fd, data, PAGE_SIZE, page_offset(page_num));
    if (written != (ssize_t)PAGE_SIZE) {
        die("pwrite");
    }
}

/* Fills a buffer from disk. A page past the end of the file has never been
 * written, so it reads as zeros; anything between 0 and a full page is a
 * torn write. */
static void load_page(Pager *pager, uint32_t page_num, void *data) {
    ssize_t got = pread(pager->fd, data, PAGE_SIZE, page_offset(page_num));
    if (got == 0) {
        memset(data, 0, PAGE_SIZE);
    } else if (got != (ssize_t)PAGE_SIZE) {
        die("pread");
    }
}

/* Picks a frame for a page that isn't cached: an empty one if there is one,
 * otherwise the least recently used frame nobody has pinned. */
static Frame *claim_frame(Pager *pager) {
    Frame *victim = NULL;
    for (uint32_t i = 0; i < pager->num_frames; i++) {
        Frame *frame = &pager->frames[i];
        if (frame->page_num == PAGER_NO_PAGE) {
            return frame;
        }
        if (frame->pin_count == 0 && (victim == NULL || frame->last_used < victim->last_used)) {
            victim = frame;
        }
    }
    if (victim == NULL) {
        fprintf(stderr, "Pager bug: all %u cache frames are pinned.\n", pager->num_frames);
        abort();
    }
    if (victim->dirty) {
        write_page(pager, victim->page_num, victim->data);
    }
    pager->evictions++;
    victim->page_num = PAGER_NO_PAGE;
    victim->dirty = false;
    return victim;
}

void *pager_get_page(Pager *pager, uint32_t page_num) {
    if (page_num == PAGER_NO_PAGE) {
        pin_bug("request for the empty-frame marker", page_num);
    }
    Frame *frame = find_frame(pager, page_num);
    if (frame != NULL) {
        pager->hits++;
    } else {
        pager->misses++;
        frame = claim_frame(pager);
        load_page(pager, page_num, frame->data);
        frame->page_num = page_num;
        if (page_num >= pager->num_pages) {
            pager->num_pages = page_num + 1;
        }
    }
    frame->pin_count++;
    frame->last_used = ++pager->clock;
    return frame->data;
}

void pager_unpin(Pager *pager, uint32_t page_num) {
    Frame *frame = find_frame(pager, page_num);
    if (frame == NULL || frame->pin_count == 0) {
        pin_bug("unpin of a page that isn't pinned", page_num);
    }
    frame->pin_count--;
}

void pager_mark_dirty(Pager *pager, uint32_t page_num) {
    Frame *frame = find_frame(pager, page_num);
    /* Writing to an unpinned page means the caller kept a stale pointer. */
    if (frame == NULL || frame->pin_count == 0) {
        pin_bug("page marked dirty while not pinned", page_num);
    }
    frame->dirty = true;
}

uint32_t pager_get_unused_page_num(const Pager *pager) {
    if (pager->num_pages >= PAGER_NO_PAGE) {
        fprintf(stderr, "Database is full: no page numbers left.\n");
        exit(EXIT_FAILURE);
    }
    return pager->num_pages;
}

void pager_flush_all(Pager *pager) {
    for (uint32_t i = 0; i < pager->num_frames; i++) {
        Frame *frame = &pager->frames[i];
        if (frame->page_num != PAGER_NO_PAGE && frame->dirty) {
            write_page(pager, frame->page_num, frame->data);
            frame->dirty = false;
        }
    }
}

void pager_close(Pager *pager) {
    for (uint32_t i = 0; i < pager->num_frames; i++) {
        if (pager->frames[i].pin_count != 0) {
            pin_bug("page still pinned at close", pager->frames[i].page_num);
        }
        free(pager->frames[i].data);
    }
    free(pager->frames);
    if (close(pager->fd) < 0) {
        die("close");
    }
    free(pager);
}
