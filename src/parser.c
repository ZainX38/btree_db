#include "parser.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "tokenizer.h"

/* Big enough for a 32-bit id written out in full; anything longer is out of
 * range anyway, and checking first keeps the copy below bounded. */
#define ID_TEXT_MAX_LEN 10

typedef struct {
    const TokenList *list;
    size_t pos;
    /* Only the first failure is kept, since later ones are usually knock-on
     * effects of it. */
    PrepareResult result;
    ParseError *error;
} Parser;

static const Token *peek(const Parser *p) {
    return &p->list->tokens[p->pos];
}

/* Never moves past TOKEN_END, so peek() is always safe after a failure. */
static const Token *advance(Parser *p) {
    const Token *token = peek(p);
    if (token->type != TOKEN_END) {
        p->pos++;
    }
    return token;
}

static bool fail_at(Parser *p, const Token *token, PrepareResult result, const char *message) {
    if (p->result == PREPARE_SUCCESS) {
        p->result = result;
        p->error->column = token->column;
        p->error->message = message;
    }
    return false;
}

/* Most errors are about the token the parser is looking at right now. */
static bool fail(Parser *p, PrepareResult result, const char *message) {
    return fail_at(p, peek(p), result, message);
}

static bool accept_keyword(Parser *p, Keyword keyword) {
    if (peek(p)->type == TOKEN_KEYWORD && peek(p)->keyword == keyword) {
        advance(p);
        return true;
    }
    return false;
}

static bool accept_symbol(Parser *p, const char *symbol) {
    if (token_is_symbol(peek(p), symbol)) {
        advance(p);
        return true;
    }
    return false;
}

static bool expect_keyword(Parser *p, Keyword keyword, const char *message) {
    return accept_keyword(p, keyword) || fail(p, PREPARE_SYNTAX_ERROR, message);
}

static bool expect_symbol(Parser *p, const char *symbol, const char *message) {
    return accept_symbol(p, symbol) || fail(p, PREPARE_SYNTAX_ERROR, message);
}

static bool word_equals(const Token *token, const char *word) {
    return token->type == TOKEN_WORD && token->length == strlen(word) &&
           strncasecmp(token->start, word, token->length) == 0;
}

static bool is_value(const Token *token) {
    return token->type == TOKEN_WORD || token->type == TOKEN_STRING;
}

static const Token *expect_value(Parser *p) {
    if (!is_value(peek(p))) {
        fail(p, PREPARE_SYNTAX_ERROR, "expected a value");
        return NULL;
    }
    return advance(p);
}

/* strtoul alone accepts "-1" (wrapping it to a huge number) and " 12abc", so
 * we check every character is a digit and the value fits in 32 bits. */
static bool parse_id(Parser *p, uint32_t *out) {
    const Token *token = expect_value(p);
    if (token == NULL) {
        return false;
    }
    if (token->type != TOKEN_WORD || token->length > ID_TEXT_MAX_LEN ||
        strspn(token->start, "0123456789") < token->length) {
        return fail_at(p, token, PREPARE_INVALID_ID, NULL);
    }
    char text[ID_TEXT_MAX_LEN + 1];
    token_copy_text(token, text);
    errno = 0;
    unsigned long value = strtoul(text, NULL, 10);
    if (errno == ERANGE || value > UINT32_MAX) {
        return fail_at(p, token, PREPARE_INVALID_ID, NULL);
    }
    *out = (uint32_t)value;
    return true;
}

static bool parse_string(Parser *p, char *dest, size_t max_len) {
    const Token *token = expect_value(p);
    if (token == NULL) {
        return false;
    }
    if (token_text_length(token) > max_len) {
        return fail_at(p, token, PREPARE_STRING_TOO_LONG, NULL);
    }
    token_copy_text(token, dest);
    return true;
}

/* There is only one table, but naming it keeps the syntax honest SQL. */
static bool parse_table_name(Parser *p) {
    if (!word_equals(peek(p), TABLE_NAME)) {
        return fail(p, PREPARE_SYNTAX_ERROR, "expected the table name '" TABLE_NAME "'");
    }
    advance(p);
    return true;
}

static bool parse_row_values(Parser *p, Row *row, const char *separator) {
    return parse_id(p, &row->id) &&
           (separator == NULL || expect_symbol(p, separator, "expected ','")) &&
           parse_string(p, row->username, USERNAME_MAX_LEN) &&
           (separator == NULL || expect_symbol(p, separator, "expected ','")) &&
           parse_string(p, row->email, EMAIL_MAX_LEN);
}

static bool parse_insert(Parser *p, Statement *statement) {
    statement->type = STATEMENT_INSERT;
    Row *row = &statement->row_to_insert;
    if (!accept_keyword(p, KEYWORD_INTO)) {
        return parse_row_values(p, row, NULL);
    }
    return parse_table_name(p) && expect_keyword(p, KEYWORD_VALUES, "expected VALUES") &&
           expect_symbol(p, "(", "expected '('") && parse_row_values(p, row, ",") &&
           expect_symbol(p, ")", "expected ')'");
}

static bool parse_column(Parser *p, Column *column) {
    static const struct {
        const char *name;
        Column column;
    } columns[] = {{"id", COLUMN_ID}, {"username", COLUMN_USERNAME}, {"email", COLUMN_EMAIL}};
    for (size_t i = 0; i < sizeof(columns) / sizeof(columns[0]); i++) {
        if (word_equals(peek(p), columns[i].name)) {
            advance(p);
            *column = columns[i].column;
            return true;
        }
    }
    return fail(p, PREPARE_SYNTAX_ERROR, "expected a column (id, username or email)");
}

static bool parse_compare_op(Parser *p, CompareOp *op) {
    static const struct {
        const char *symbol;
        CompareOp op;
    } ops[] = {{"=", COMPARE_EQ}, {"!=", COMPARE_NE}, {"<", COMPARE_LT},
               {"<=", COMPARE_LE}, {">", COMPARE_GT}, {">=", COMPARE_GE}};
    for (size_t i = 0; i < sizeof(ops) / sizeof(ops[0]); i++) {
        if (accept_symbol(p, ops[i].symbol)) {
            *op = ops[i].op;
            return true;
        }
    }
    return fail(p, PREPARE_SYNTAX_ERROR, "expected a comparison (=, !=, <, <=, >, >=)");
}

static bool parse_where(Parser *p, WhereClause *where) {
    where->present = true;
    if (!parse_column(p, &where->column) || !parse_compare_op(p, &where->op)) {
        return false;
    }
    if (where->column == COLUMN_ID) {
        return parse_id(p, &where->id);
    }
    size_t max_len = where->column == COLUMN_USERNAME ? USERNAME_MAX_LEN : EMAIL_MAX_LEN;
    return parse_string(p, where->text, max_len);
}

static bool parse_select(Parser *p, Statement *statement) {
    statement->type = STATEMENT_SELECT;
    if (accept_symbol(p, "*") &&
        !(expect_keyword(p, KEYWORD_FROM, "expected FROM") && parse_table_name(p))) {
        return false;
    }
    if (accept_keyword(p, KEYWORD_WHERE)) {
        return parse_where(p, &statement->where);
    }
    return true;
}

static bool parse_statement(Parser *p, Statement *statement) {
    bool parsed;
    if (accept_keyword(p, KEYWORD_INSERT)) {
        parsed = parse_insert(p, statement);
    } else if (accept_keyword(p, KEYWORD_SELECT)) {
        parsed = parse_select(p, statement);
    } else {
        return fail(p, PREPARE_UNRECOGNIZED_STATEMENT, NULL);
    }
    if (!parsed) {
        return false;
    }
    accept_symbol(p, ";");
    return peek(p)->type == TOKEN_END ||
           fail(p, PREPARE_SYNTAX_ERROR, "expected end of statement");
}

static const char *tokenize_error_message(TokenizeResult result) {
    return result == TOKENIZE_UNTERMINATED_STRING ? "string is never closed" : "too many tokens";
}

PrepareResult prepare_statement(const char *input, Statement *statement, ParseError *error) {
    memset(statement, 0, sizeof(*statement));
    memset(error, 0, sizeof(*error));
    TokenList list;
    TokenizeResult tokenized = tokenize(input, &list, &error->column);
    if (tokenized != TOKENIZE_SUCCESS) {
        error->message = tokenize_error_message(tokenized);
        return PREPARE_SYNTAX_ERROR;
    }
    Parser parser = {.list = &list, .pos = 0, .result = PREPARE_SUCCESS, .error = error};
    parse_statement(&parser, statement);
    return parser.result;
}
