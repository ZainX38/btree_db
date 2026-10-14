#ifndef CRC32_H
#define CRC32_H

/*
 * CRC-32 (the IEEE polynomial used by zlib, gzip and PNG), so WAL records can
 * be checked with Python's zlib.crc32 in tests.
 */

#include <stddef.h>
#include <stdint.h>

/* Start value for crc32_update; pass the result of one call into the next to
 * checksum data that is split across buffers. */
#define CRC32_INIT 0u

uint32_t crc32_update(uint32_t crc, const void *data, size_t length);

#endif
