#include "dynphony/frontend.h"

static int dyn_space(char value) {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

static int dyn_digit(char value) {
    return value >= '0' && value <= '9';
}

static int dyn_hex_digit(char value) {
    return dyn_digit(value)
        || (value >= 'a' && value <= 'f')
        || (value >= 'A' && value <= 'F');
}

static unsigned int dyn_hex_value(char value) {
    if (dyn_digit(value)) return (unsigned int)(value - '0');
    if (value >= 'a' && value <= 'f')
        return (unsigned int)(value - 'a') + 10u;
    return (unsigned int)(value - 'A') + 10u;
}

static int dyn_identifier_start(char value) {
    return (value >= 'a' && value <= 'z')
        || (value >= 'A' && value <= 'Z')
        || value == '_';
}

static int dyn_identifier_part(char value) {
    return dyn_identifier_start(value) || dyn_digit(value);
}

static int dyn_word(
    const struct DynLexer *lexer,
    unsigned int start,
    unsigned int length,
    const char *word
) {
    unsigned int index = 0;
    while (word[index]) {
        if (index == length || lexer->source[start + index] != word[index])
            return 0;
        index += 1;
    }
    return index == length;
}

static void dyn_token(
    struct DynLexer *lexer,
    int kind,
    unsigned int position,
    unsigned int value
) {
    lexer->current.kind = kind;
    lexer->current.position = position;
    lexer->current.value = value;
}

static void dyn_skip(struct DynLexer *lexer) {
    int again = 1;
    while (again) {
        again = 0;
        while (
            lexer->position < lexer->length
            && dyn_space(lexer->source[lexer->position])
        ) lexer->position += 1;
        if (
            lexer->position + 1 < lexer->length
            && lexer->source[lexer->position] == '/'
            && lexer->source[lexer->position + 1] == '/'
        ) {
            lexer->position += 2;
            while (
                lexer->position < lexer->length
                && lexer->source[lexer->position] != '\n'
            ) lexer->position += 1;
            again = 1;
        } else if (
            lexer->position + 1 < lexer->length
            && lexer->source[lexer->position] == '/'
            && lexer->source[lexer->position + 1] == '*'
        ) {
            lexer->position += 2;
            while (
                lexer->position + 1 < lexer->length
                && !(lexer->source[lexer->position] == '*'
                    && lexer->source[lexer->position + 1] == '/')
            ) lexer->position += 1;
            if (lexer->position + 1 >= lexer->length) {
                lexer->error = 1;
                return;
            }
            lexer->position += 2;
            again = 1;
        }
    }
}

void dyn_lexer_init(
    struct DynLexer *lexer,
    const char *source,
    unsigned int length
) {
    lexer->source = source;
    lexer->length = length;
    lexer->position = 0;
    lexer->error = 0;
    dyn_lexer_next(lexer);
}

void dyn_lexer_next(struct DynLexer *lexer) {
    unsigned int start;
    unsigned int value;
    unsigned int base;
    char first;
    dyn_skip(lexer);
    start = lexer->position;
    if (lexer->error) {
        dyn_token(lexer, DYN_TOK_INVALID, start, 0);
        return;
    }
    if (start >= lexer->length) {
        dyn_token(lexer, DYN_TOK_EOF, start, 0);
        return;
    }
    first = lexer->source[lexer->position];
    if (dyn_identifier_start(first)) {
        lexer->position += 1;
        while (
            lexer->position < lexer->length
            && dyn_identifier_part(lexer->source[lexer->position])
        ) lexer->position += 1;
        if (dyn_word(lexer, start, lexer->position - start, "int"))
            dyn_token(lexer, DYN_TOK_INT, start, 0);
        else if (dyn_word(lexer, start, lexer->position - start, "void"))
            dyn_token(lexer, DYN_TOK_VOID, start, 0);
        else if (dyn_word(lexer, start, lexer->position - start, "main"))
            dyn_token(lexer, DYN_TOK_MAIN, start, 0);
        else if (dyn_word(lexer, start, lexer->position - start, "return"))
            dyn_token(lexer, DYN_TOK_RETURN, start, 0);
        else {
            lexer->error = 1;
            dyn_token(lexer, DYN_TOK_INVALID, start, 0);
        }
        return;
    }
    if (dyn_digit(first)) {
        value = 0;
        base = 10;
        if (first == '0') {
            base = 8;
            lexer->position += 1;
            if (
                lexer->position < lexer->length
                && (lexer->source[lexer->position] == 'x'
                    || lexer->source[lexer->position] == 'X')
            ) {
                base = 16;
                lexer->position += 1;
            }
        }
        while (lexer->position < lexer->length) {
            char digit = lexer->source[lexer->position];
            if (!dyn_hex_digit(digit) || dyn_hex_value(digit) >= base) break;
            value = value * base + dyn_hex_value(digit);
            lexer->position += 1;
        }
        dyn_token(lexer, DYN_TOK_NUMBER, start, value);
        return;
    }
    lexer->position += 1;
    if (first == '(') dyn_token(lexer, DYN_TOK_LPAREN, start, 0);
    else if (first == ')') dyn_token(lexer, DYN_TOK_RPAREN, start, 0);
    else if (first == '{') dyn_token(lexer, DYN_TOK_LBRACE, start, 0);
    else if (first == '}') dyn_token(lexer, DYN_TOK_RBRACE, start, 0);
    else if (first == ';') dyn_token(lexer, DYN_TOK_SEMICOLON, start, 0);
    else if (first == '+') dyn_token(lexer, DYN_TOK_PLUS, start, 0);
    else if (first == '-') dyn_token(lexer, DYN_TOK_MINUS, start, 0);
    else if (first == '*') dyn_token(lexer, DYN_TOK_STAR, start, 0);
    else if (first == '/') dyn_token(lexer, DYN_TOK_SLASH, start, 0);
    else if (first == '%') dyn_token(lexer, DYN_TOK_PERCENT, start, 0);
    else if (first == '&') dyn_token(lexer, DYN_TOK_AMP, start, 0);
    else if (first == '|') dyn_token(lexer, DYN_TOK_PIPE, start, 0);
    else if (first == '^') dyn_token(lexer, DYN_TOK_CARET, start, 0);
    else if (first == '~') dyn_token(lexer, DYN_TOK_TILDE, start, 0);
    else if (
        first == '<' && lexer->position < lexer->length
        && lexer->source[lexer->position] == '<'
    ) {
        lexer->position += 1;
        dyn_token(lexer, DYN_TOK_LSHIFT, start, 0);
    } else if (
        first == '>' && lexer->position < lexer->length
        && lexer->source[lexer->position] == '>'
    ) {
        lexer->position += 1;
        dyn_token(lexer, DYN_TOK_RSHIFT, start, 0);
    } else {
        lexer->error = 1;
        dyn_token(lexer, DYN_TOK_INVALID, start, 0);
    }
}
