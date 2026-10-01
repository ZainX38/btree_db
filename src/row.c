#include "row.h"

#include <stdio.h>
#include <string.h>

_Static_assert(ROW_SIZE == 293, "on-disk row layout must stay at 293 bytes");

/* Copies a string into a fixed-size field and zero-fills the rest, so the same
 * row always serializes to the same bytes (stale bytes would otherwise leak
 * onto disk and make checksums of identical rows differ). */
static void write_string_field(char *field, uint32_t field_size, const char *value) {
    size_t len = strnlen(value, field_size - 1);
    memset(field, 0, field_size);
    memcpy(field, value, len);
}

void serialize_row(const Row *source, void *destination) {
    char *dest = destination;
    memcpy(dest + ROW_ID_OFFSET, &source->id, ROW_ID_SIZE);
    write_string_field(dest + ROW_USERNAME_OFFSET, ROW_USERNAME_SIZE, source->username);
    write_string_field(dest + ROW_EMAIL_OFFSET, ROW_EMAIL_SIZE, source->email);
}

void deserialize_row(const void *source, Row *destination) {
    const char *src = source;
    memcpy(&destination->id, src + ROW_ID_OFFSET, ROW_ID_SIZE);
    memcpy(destination->username, src + ROW_USERNAME_OFFSET, ROW_USERNAME_SIZE);
    memcpy(destination->email, src + ROW_EMAIL_OFFSET, ROW_EMAIL_SIZE);
    /* Never trust bytes read back from storage to be terminated. */
    destination->username[USERNAME_MAX_LEN] = '\0';
    destination->email[EMAIL_MAX_LEN] = '\0';
}

void print_row(const Row *row) {
    printf("(%u, %s, %s)\n", row->id, row->username, row->email);
}
