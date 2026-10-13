#include "tokenizer.h"

#include <ctype.h>
#include <string.h>
#include <strings.h>

#define QUOTE '\''
/* Characters that end an unquoted word, so "(1,a)" splits without spaces. */
#define SYMBOL_CHARS "(),;*=<>!"
#define TWO_CHAR_OPERATOR_LEN 2

typedef struct {
    const char *text;
    Keyword keyword;
} KeywordEntry;

static const KeywordEntry KEYWORDS[] = {
    {"insert", KEYWORD_INSERT},
    {"select", KEYWORD_SELECT},
};

static bool is_symbol_char(char c) {
    return c != '\0' && strchr(SYMBOL_CHARS, c) != NULL;
}

static bool ends_word(char c) {
    return c == '\0' || isspace((unsigned char)c) || c == QUOTE || is_symbol_char(c);
}

/* SQL keywords are case-insensitive, so INSERT, insert and Insert all match. */
static Keyword lookup_keyword(const char *start, size_t length) {
    for (size_t i = 0; i < sizeof(KEYWORDS) / sizeof(KEYWORDS[0]); i++) {
        if (strlen(KEYWORDS[i].text) == length &&
            strncasecmp(KEYWORDS[i].text, start, length) == 0) {
            return KEYWORDS[i].keyword;
        }
    }
    return KEYWORD_NONE;
}

static size_t scan_word(const char *p, Token *token) {
    size_t length = 0;
    while (!ends_word(p[length])) {
        length++;
    }
    token->keyword = lookup_keyword(p, length);
    token->type = token->keyword == KEYWORD_NONE ? TOKEN_WORD : TOKEN_KEYWORD;
    token->start = p;
    token->length = length;
    return length;
}

/* A doubled quote inside a string is an escaped quote, as in standard SQL.
 * Returns how many input characters the string used, or 0 if it never ends. */
static size_t scan_string(const char *p, Token *token) {
    size_t i = 1;
    for (;;) {
        if (p[i] == '\0') {
            return 0;
        }
        if (p[i] == QUOTE && p[i + 1] == QUOTE) {
            i += 2;
        } else if (p[i] == QUOTE) {
            break;
        } else {
            i++;
        }
    }
    token->type = TOKEN_STRING;
    token->start = p + 1;
    token->length = i - 1;
    return i + 1;
}

static size_t scan_symbol(const char *p, Token *token) {
    bool two_char = (p[0] == '<' || p[0] == '>' || p[0] == '!') && p[1] == '=';
    token->type = TOKEN_SYMBOL;
    token->start = p;
    token->length = two_char ? TWO_CHAR_OPERATOR_LEN : 1;
    return token->length;
}

TokenizeResult tokenize(const char *input, TokenList *list, size_t *error_column) {
    const char *p = input;
    list->count = 0;
    for (;;) {
        while (isspace((unsigned char)*p)) {
            p++;
        }
        if (list->count == MAX_TOKENS) {
            *error_column = (size_t)(p - input) + 1;
            return TOKENIZE_TOO_MANY_TOKENS;
        }
        Token *token = &list->tokens[list->count++];
        memset(token, 0, sizeof(*token));
        token->column = (size_t)(p - input) + 1;
        if (*p == '\0') {
            token->type = TOKEN_END;
            token->start = p;
            return TOKENIZE_SUCCESS;
        }
        size_t used;
        if (*p == QUOTE) {
            used = scan_string(p, token);
        } else if (is_symbol_char(*p)) {
            used = scan_symbol(p, token);
        } else {
            used = scan_word(p, token);
        }
        if (used == 0) {
            *error_column = token->column;
            return TOKENIZE_UNTERMINATED_STRING;
        }
        p += used;
    }
}

bool token_is_symbol(const Token *token, const char *symbol) {
    return token->type == TOKEN_SYMBOL && token->length == strlen(symbol) &&
           strncmp(token->start, symbol, token->length) == 0;
}

/* Only strings can contain escaped quotes; words and symbols are verbatim. */
static size_t escaped_quote_count(const Token *token) {
    if (token->type != TOKEN_STRING) {
        return 0;
    }
    size_t count = 0;
    for (size_t i = 0; i + 1 < token->length; i++) {
        if (token->start[i] == QUOTE) {
            count++;
            i++;
        }
    }
    return count;
}

size_t token_text_length(const Token *token) {
    return token->length - escaped_quote_count(token);
}

void token_copy_text(const Token *token, char *dest) {
    size_t out = 0;
    for (size_t i = 0; i < token->length; i++) {
        dest[out++] = token->start[i];
        if (token->type == TOKEN_STRING && token->start[i] == QUOTE) {
            i++;
        }
    }
    dest[out] = '\0';
}
