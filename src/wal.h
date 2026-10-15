#ifndef WAL_H
#define WAL_H

/*
 * The write-ahead log: an append-only file next to the database
 * ("<db>.wal") that receives every changed page before the database file
 * does. A statement only counts as done once its pages and a commit record
 * are in the log and fsync has returned.
 *
 * Record layout, all integers in host byte order like the rest of the file:
 *
 *   type (4) | page_num (4) | crc32 (4) | payload
 *
 * A page record's payload is the full PAGE_SIZE image of that page; a commit
 * record has no payload. The CRC covers type, page_num and the payload, so a
 * record that was only partly written before a crash fails the check.
 */

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#define WAL_SUFFIX ".wal"

#define WAL_TYPE_OFFSET 0u
#define WAL_TYPE_SIZE ((uint32_t)sizeof(uint32_t))
#define WAL_PAGE_NUM_OFFSET (WAL_TYPE_OFFSET + WAL_TYPE_SIZE)
#define WAL_PAGE_NUM_SIZE ((uint32_t)sizeof(uint32_t))
#define WAL_CRC_OFFSET (WAL_PAGE_NUM_OFFSET + WAL_PAGE_NUM_SIZE)
#define WAL_CRC_SIZE ((uint32_t)sizeof(uint32_t))
#define WAL_HEADER_SIZE (WAL_CRC_OFFSET + WAL_CRC_SIZE)

/* Starts at 1 so an all-zero header (a hole, or a zero-filled tail) is never
 * mistaken for a record. */
typedef enum { WAL_RECORD_PAGE = 1, WAL_RECORD_COMMIT = 2 } WalRecordType;

typedef struct {
    int fd;
    off_t size; /* where the next record goes */
    uint64_t records; /* records in the log now; reset empties it */
    bool sync; /* false skips fsync, trading power-loss safety for speed */
} Wal;

Wal *wal_open(const char *db_filename, bool sync);
/* Returns the file offset of the page image, so it can be read back later. */
off_t wal_append_page(Wal *wal, uint32_t page_num, const void *data);
void wal_append_commit(Wal *wal);
/* Called once for every page image that belongs to a committed statement, in
 * log order, so a later image of the same page replaces an earlier one. */
typedef void (*WalPageVisitor)(void *context, uint32_t page_num, off_t offset);

/* Walks the log from the start until a record is truncated or fails its CRC,
 * and visits every page image that is followed by a commit record. Anything
 * after the last good commit was never acknowledged, so ignoring it loses
 * nothing a user was promised. */
void wal_replay(Wal *wal, WalPageVisitor visit, void *context);
/* Reads back a page image from the offset wal_append_page returned. */
void wal_read_page(const Wal *wal, off_t offset, void *data);
/* Makes everything appended so far durable (unless sync is off). */
void wal_sync(Wal *wal);
/* Empties the log. Only safe once every page in it has reached the
 * database file and been fsynced there. */
void wal_reset(Wal *wal);
void wal_close(Wal *wal);

#endif
