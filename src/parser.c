#include "parser.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "tokenizer.h"

/* Big enough for a 32-bit id written out in full; anything longer is out of
 * range anyway, and checking first keeps the copy below bounded. */
#define ID_TEXT_MAX_LEN 10
/* Token counts include the final TOKEN_END. */
#define INSERT_TOKEN_COUNT 5 /* insert id username email */
#define SELECT_TOKEN_COUNT 2 /* select */

/* strtoul alone accepts "-1" (wrapping it to a huge number) and " 12abc", so
 * we check every character is a digit and the value fits in 32 bits. */
static int parse_id(const Token *token, uint32_t *out) {
    if (token->type != TOKEN_WORD || token->length == 0 || token->length > ID_TEXT_MAX_LEN) {
        return -1;
    }
    if (strspn(token->start, "0123456789") < token->length) {
        return -1;
    }
    char text[ID_TEXT_MAX_LEN + 1];
    token_copy_text(token, text);
    errno = 0;
    unsigned long value = strtoul(text, NULL, 10);
    if (errno == ERANGE || value > UINT32_MAX) {
        return -1;
    }
    *out = (uint32_t)value;
    return 0;
}

static bool is_value(const Token *token) {
    return token->type == TOKEN_WORD || token->type == TOKEN_STRING;
}

static int copy_string(const Token *token, char *dest, size_t max_len) {
    if (token_text_length(token) > max_len) {
        return -1;
    }
    token_copy_text(token, dest);
    return 0;
}

/* insert <id> <username> <email>, where the strings may be quoted. */
static PrepareResult prepare_insert(const TokenList *list, Statement *statement) {
    const Token *t = list->tokens;
    if (list->count != INSERT_TOKEN_COUNT || !is_value(&t[1]) || !is_value(&t[2]) ||
        !is_value(&t[3])) {
        return PREPARE_SYNTAX_ERROR;
    }
    Row *row = &statement->row_to_insert;
    if (parse_id(&t[1], &row->id) != 0) {
        return PREPARE_INVALID_ID;
    }
    if (copy_string(&t[2], row->username, USERNAME_MAX_LEN) != 0 ||
        copy_string(&t[3], row->email, EMAIL_MAX_LEN) != 0) {
        return PREPARE_STRING_TOO_LONG;
    }
    statement->type = STATEMENT_INSERT;
    return PREPARE_SUCCESS;
}

PrepareResult prepare_statement(const char *input, Statement *statement) {
    memset(statement, 0, sizeof(*statement));
    TokenList list;
    size_t error_column;
    if (tokenize(input, &list, &error_column) != TOKENIZE_SUCCESS) {
        return PREPARE_SYNTAX_ERROR;
    }
    const Token *first = &list.tokens[0];
    if (first->type != TOKEN_KEYWORD) {
        return PREPARE_UNRECOGNIZED_STATEMENT;
    }
    if (first->keyword == KEYWORD_INSERT) {
        return prepare_insert(&list, statement);
    }
    if (list.count != SELECT_TOKEN_COUNT) {
        return PREPARE_SYNTAX_ERROR;
    }
    statement->type = STATEMENT_SELECT;
    return PREPARE_SUCCESS;
}
