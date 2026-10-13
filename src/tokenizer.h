#ifndef TOKENIZER_H
#define TOKENIZER_H

/*
 * Splits one line of input into tokens.
 *
 * Tokens point back into the input instead of copying it, so the input must
 * outlive them and is never modified. Quoted strings keep their doubled
 * quotes ('it''s'); token_copy_text undoes that when the value is used, which
 * keeps the tokenizer free of allocation.
 */

#include <stdbool.h>
#include <stddef.h>

/* Long enough for any statement the grammar accepts; anything longer is a
 * syntax error rather than a reason to allocate. */
#define MAX_TOKENS 32

typedef enum {
    TOKEN_KEYWORD,
    TOKEN_WORD,   /* unquoted run of characters: a number, name or bare value */
    TOKEN_STRING, /* contents of '...', without the outer quotes */
    TOKEN_SYMBOL, /* one punctuation character, or a two-character operator */
    TOKEN_END,
} TokenType;

typedef enum {
    KEYWORD_NONE,
    KEYWORD_INSERT,
    KEYWORD_SELECT,
} Keyword;

typedef struct {
    TokenType type;
    Keyword keyword; /* only meaningful for TOKEN_KEYWORD */
    const char *start;
    size_t length;
    /* 1-based position in the input, for pointing at errors. */
    size_t column;
} Token;

typedef struct {
    Token tokens[MAX_TOKENS];
    size_t count; /* includes the final TOKEN_END */
} TokenList;

typedef enum {
    TOKENIZE_SUCCESS,
    TOKENIZE_UNTERMINATED_STRING,
    TOKENIZE_TOO_MANY_TOKENS,
} TokenizeResult;

/* On failure, `error_column` is where the problem starts. */
TokenizeResult tokenize(const char *input, TokenList *list, size_t *error_column);

bool token_is_symbol(const Token *token, const char *symbol);

/* Length of the token's value once doubled quotes are collapsed. */
size_t token_text_length(const Token *token);
/* Writes the value plus a NUL; `dest` needs token_text_length() + 1 bytes. */
void token_copy_text(const Token *token, char *dest);

#endif
