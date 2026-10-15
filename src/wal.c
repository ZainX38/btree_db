#include "wal.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "crc32.h"
#include "pager.h"

/* Same permissions as the database: the log holds the same data. */
#define WAL_FILE_MODE (S_IRUSR | S_IWUSR)

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
