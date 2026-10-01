#ifndef ROW_H
#define ROW_H

/*
 * A row is the single record type this database stores. It has an in-memory
 * form (Row) and a fixed 293-byte on-disk form; the two differ because the
 * compiler may add padding to the struct, and we never want padding on disk.
 */

#include <stdint.h>

#define USERNAME_MAX_LEN 32
#define EMAIL_MAX_LEN 255

typedef struct {
    uint32_t id;
    /* +1 for the NUL terminator, so a full-length string still fits. */
    char username[USERNAME_MAX_LEN + 1];
    char email[EMAIL_MAX_LEN + 1];
} Row;

/* Serialized layout: id | username | email, packed with no gaps. */
#define ROW_ID_SIZE ((uint32_t)sizeof(uint32_t))
#define ROW_USERNAME_SIZE ((uint32_t)(USERNAME_MAX_LEN + 1))
#define ROW_EMAIL_SIZE ((uint32_t)(EMAIL_MAX_LEN + 1))
#define ROW_ID_OFFSET 0u
#define ROW_USERNAME_OFFSET (ROW_ID_OFFSET + ROW_ID_SIZE)
#define ROW_EMAIL_OFFSET (ROW_USERNAME_OFFSET + ROW_USERNAME_SIZE)
#define ROW_SIZE (ROW_EMAIL_OFFSET + ROW_EMAIL_SIZE)

void serialize_row(const Row *source, void *destination);
void deserialize_row(const void *source, Row *destination);
void print_row(const Row *row);

#endif
