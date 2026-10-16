#include "wal.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "crashpoint.h"
#include "crc32.h"
#include "pager.h"

/* Same permissions as the database: the log holds the same data. */
#define WAL_FILE_MODE (S_IRUSR | S_IWUSR)
/* Most statements change only a page or two. */
#define PENDING_INITIAL_CAPACITY 16u

/* A log we can't write is a database we can't promise anything about. */
static void die(const char *what) {
    perror(what);
    exit(EXIT_FAILURE);
}

static void write_u32(unsigned char *buf, uint32_t offset, uint32_t value) {
    memcpy(buf + offset, &value, sizeof(value));
}

static char *wal_filename(const char *db_filename) {
    size_t length = strlen(db_filename) + strlen(WAL_SUFFIX) + 1;
    char *name = malloc(length);
    if (name == NULL) {
        die("malloc");
    }
    snprintf(name, length, "%s%s", db_filename, WAL_SUFFIX);
    return name;
}

Wal *wal_open(const char *db_filename, bool sync) {
    char *name = wal_filename(db_filename);
    int fd = open(name, O_RDWR | O_CREAT, WAL_FILE_MODE);
    free(name);
    if (fd < 0) {
        die("open wal");
    }
    struct stat st;
    if (fstat(fd, &st) < 0) {
        die("fstat wal");
    }
    Wal *wal = calloc(1, sizeof(Wal));
    if (wal == NULL) {
        die("calloc");
    }
    wal->fd = fd;
    wal->size = st.st_size;
    wal->sync = sync;
    return wal;
}

static void write_at(Wal *wal, const void *data, size_t length, off_t offset) {
    crash_point();
    if (pwrite(wal->fd, data, length, offset) != (ssize_t)length) {
        die("pwrite wal");
    }
}

/* Writes one record at the end of the log and returns where its payload is. */
static off_t append_record(Wal *wal, WalRecordType type, uint32_t page_num, const void *payload,
                           size_t payload_size) {
    unsigned char header[WAL_HEADER_SIZE];
    write_u32(header, WAL_TYPE_OFFSET, (uint32_t)type);
    write_u32(header, WAL_PAGE_NUM_OFFSET, page_num);
    uint32_t crc = crc32_update(CRC32_INIT, header, WAL_CRC_OFFSET);
    crc = crc32_update(crc, payload, payload_size);
    write_u32(header, WAL_CRC_OFFSET, crc);

    off_t payload_offset = wal->size + WAL_HEADER_SIZE;
    write_at(wal, header, WAL_HEADER_SIZE, wal->size);
    if (payload_size > 0) {
        write_at(wal, payload, payload_size, payload_offset);
    }
    wal->size = payload_offset + (off_t)payload_size;
    wal->records++;
    return payload_offset;
}

off_t wal_append_page(Wal *wal, uint32_t page_num, const void *data) {
    return append_record(wal, WAL_RECORD_PAGE, page_num, data, PAGE_SIZE);
}

void wal_append_commit(Wal *wal) {
    append_record(wal, WAL_RECORD_COMMIT, 0, NULL, 0);
}

static uint32_t read_u32(const unsigned char *buf, uint32_t offset) {
    uint32_t value;
    memcpy(&value, buf + offset, sizeof(value));
    return value;
}

static bool read_exact(const Wal *wal, void *data, size_t length, off_t offset) {
    return pread(wal->fd, data, length, offset) == (ssize_t)length;
}

/* Reads and checks the record at `offset`. Returns its total size, or 0 if
 * it is cut short, has an unknown type or fails its CRC: any of those means
 * the write was interrupted, so nothing from here on can be trusted. */
static off_t check_record(const Wal *wal, off_t offset, uint32_t *type, uint32_t *page_num,
                          void *payload) {
    unsigned char header[WAL_HEADER_SIZE];
    if (!read_exact(wal, header, WAL_HEADER_SIZE, offset)) {
        return 0;
    }
    *type = read_u32(header, WAL_TYPE_OFFSET);
    *page_num = read_u32(header, WAL_PAGE_NUM_OFFSET);
    size_t payload_size;
    if (*type == WAL_RECORD_PAGE) {
        payload_size = PAGE_SIZE;
    } else if (*type == WAL_RECORD_COMMIT) {
        payload_size = 0;
    } else {
        return 0;
    }
    if (payload_size > 0 && !read_exact(wal, payload, payload_size, offset + WAL_HEADER_SIZE)) {
        return 0;
    }
    uint32_t crc = crc32_update(CRC32_INIT, header, WAL_CRC_OFFSET);
    crc = crc32_update(crc, payload, payload_size);
    if (crc != read_u32(header, WAL_CRC_OFFSET)) {
        return 0;
    }
    return WAL_HEADER_SIZE + (off_t)payload_size;
}

/* Page images seen since the last commit record. They only count once a
 * commit record follows them. */
typedef struct {
    uint32_t page_num;
    off_t offset;
} PendingPage;

typedef struct {
    PendingPage *items;
    size_t count;
    size_t capacity;
} PendingPages;

static void add_pending(PendingPages *pending, uint32_t page_num, off_t offset) {
    if (pending->count == pending->capacity) {
        /* Doubling keeps growth cheap; a big split logs hundreds of pages. */
        pending->capacity =
            pending->capacity == 0 ? PENDING_INITIAL_CAPACITY : 2 * pending->capacity;
        pending->items = realloc(pending->items, pending->capacity * sizeof(PendingPage));
        if (pending->items == NULL) {
            die("realloc");
        }
    }
    pending->items[pending->count++] = (PendingPage){page_num, offset};
}

void wal_replay(Wal *wal, WalPageVisitor visit, void *context) {
    PendingPages pending = {0};
    unsigned char *payload = malloc(PAGE_SIZE);
    if (payload == NULL) {
        die("malloc");
    }
    off_t offset = 0;
    uint32_t type;
    uint32_t page_num;
    off_t size;
    while ((size = check_record(wal, offset, &type, &page_num, payload)) != 0) {
        if (type == WAL_RECORD_PAGE) {
            add_pending(&pending, page_num, offset + WAL_HEADER_SIZE);
        } else {
            for (size_t i = 0; i < pending.count; i++) {
                visit(context, pending.items[i].page_num, pending.items[i].offset);
            }
            pending.count = 0;
        }
        offset += size;
    }
    free(payload);
    free(pending.items);
}

void wal_read_page(const Wal *wal, off_t offset, void *data) {
    if (pread(wal->fd, data, PAGE_SIZE, offset) != (ssize_t)PAGE_SIZE) {
        die("pread wal");
    }
}

void wal_reset(Wal *wal) {
    if (wal->size == 0) {
        return;
    }
    crash_point();
    if (ftruncate(wal->fd, 0) < 0) {
        die("ftruncate wal");
    }
    wal->size = 0;
    wal->records = 0;
    wal_sync(wal);
}

void wal_sync(Wal *wal) {
    if (wal->sync && fsync(wal->fd) < 0) {
        die("fsync wal");
    }
}

void wal_close(Wal *wal) {
    if (close(wal->fd) < 0) {
        die("close wal");
    }
    free(wal);
}
