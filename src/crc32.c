#include "crc32.h"

#include <stdbool.h>

/* The IEEE polynomial with its bits reversed, because the bytes are fed in
 * least significant bit first. */
#define CRC32_POLYNOMIAL 0xEDB88320u
#define BYTE_VALUES 256
#define BITS_PER_BYTE 8
#define BYTE_MASK 0xFFu

static uint32_t table[BYTE_VALUES];
static bool table_ready = false;

/* One entry per byte value: what dividing that byte alone leaves behind.
 * The table turns eight shift-and-xor steps per byte into one lookup. */
static void build_table(void) {
    for (uint32_t byte = 0; byte < BYTE_VALUES; byte++) {
        uint32_t crc = byte;
        for (int bit = 0; bit < BITS_PER_BYTE; bit++) {
            crc = (crc & 1u) ? (crc >> 1) ^ CRC32_POLYNOMIAL : crc >> 1;
        }
        table[byte] = crc;
    }
    table_ready = true;
}

uint32_t crc32_update(uint32_t crc, const void *data, size_t length) {
    if (!table_ready) {
        build_table();
    }
    const unsigned char *bytes = data;
    /* The standard CRC-32 inverts the value before and after, so leading zero
     * bytes still change the result. */
    crc = ~crc;
    for (size_t i = 0; i < length; i++) {
        crc = table[(crc ^ bytes[i]) & BYTE_MASK] ^ (crc >> BITS_PER_BYTE);
    }
    return ~crc;
}
