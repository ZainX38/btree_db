#include "parser.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define WHITESPACE " \t"

/* strtoul alone accepts "-1" (wrapping it to a huge number) and " 12abc", so
 * we check every character is a digit and the value fits in 32 bits. */
static int parse_id(const char *text, uint32_t *out) {
    if (*text == '\0') {
        return -1;
    }
    for (const char *p = text; *p != '\0'; p++) {
        if (!isdigit((unsigned char)*p)) {
            return -1;
        }
    }
    errno = 0;
    unsigned long value = strtoul(text, NULL, 10);
    if (errno == ERANGE || value > UINT32_MAX) {
        return -1;
    }
    *out = (uint32_t)value;
    return 0;
}

static PrepareResult prepare_insert(Statement *statement) {
    /* strtok continues from where prepare_statement left off. */
    char *id_text = strtok(NULL, WHITESPACE);
    char *username = strtok(NULL, WHITESPACE);
    char *email = strtok(NULL, WHITESPACE);
    if (id_text == NULL || username == NULL || email == NULL || strtok(NULL, WHITESPACE)) {
        return PREPARE_SYNTAX_ERROR;
    }
    if (parse_id(id_text, &statement->row_to_insert.id) != 0) {
        return PREPARE_INVALID_ID;
    }
    if (strlen(username) > USERNAME_MAX_LEN || strlen(email) > EMAIL_MAX_LEN) {
        return PREPARE_STRING_TOO_LONG;
    }
    statement->type = STATEMENT_INSERT;
    strcpy(statement->row_to_insert.username, username);
    strcpy(statement->row_to_insert.email, email);
    return PREPARE_SUCCESS;
}

PrepareResult prepare_statement(char *input, Statement *statement) {
    memset(statement, 0, sizeof(*statement));
    char *keyword = strtok(input, WHITESPACE);
    if (keyword == NULL) {
        return PREPARE_UNRECOGNIZED_STATEMENT;
    }
    if (strcmp(keyword, "insert") == 0) {
        return prepare_insert(statement);
    }
    if (strcmp(keyword, "select") == 0) {
        if (strtok(NULL, WHITESPACE) != NULL) {
            return PREPARE_SYNTAX_ERROR;
        }
        statement->type = STATEMENT_SELECT;
        return PREPARE_SUCCESS;
    }
    return PREPARE_UNRECOGNIZED_STATEMENT;
}
