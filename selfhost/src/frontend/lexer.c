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
    lexer->current.length = lexer->position - position;
}

static int dyn_keyword(
    const struct DynLexer *lexer,
    unsigned int start,
    unsigned int length
) {
    if (dyn_word(lexer, start, length, "int")) return DYN_TOK_INT;
    if (dyn_word(lexer, start, length, "void")) return DYN_TOK_VOID;
    if (dyn_word(lexer, start, length, "main")) return DYN_TOK_MAIN;
    if (dyn_word(lexer, start, length, "return")) return DYN_TOK_RETURN;
    if (dyn_word(lexer, start, length, "unsigned")) return DYN_TOK_UNSIGNED;
    if (dyn_word(lexer, start, length, "signed")) return DYN_TOK_SIGNED;
    if (dyn_word(lexer, start, length, "char")) return DYN_TOK_CHAR_TYPE;
    if (dyn_word(lexer, start, length, "short")) return DYN_TOK_SHORT;
    if (dyn_word(lexer, start, length, "long")) return DYN_TOK_LONG;
    if (dyn_word(lexer, start, length, "struct")) return DYN_TOK_STRUCT;
    if (dyn_word(lexer, start, length, "enum")) return DYN_TOK_ENUM;
    if (dyn_word(lexer, start, length, "typedef")) return DYN_TOK_TYPEDEF;
    if (dyn_word(lexer, start, length, "static")) return DYN_TOK_STATIC;
    if (dyn_word(lexer, start, length, "extern")) return DYN_TOK_EXTERN;
    if (dyn_word(lexer, start, length, "const")) return DYN_TOK_CONST;
    if (dyn_word(lexer, start, length, "sizeof")) return DYN_TOK_SIZEOF;
    if (dyn_word(lexer, start, length, "if")) return DYN_TOK_IF;
    if (dyn_word(lexer, start, length, "else")) return DYN_TOK_ELSE;
    if (dyn_word(lexer, start, length, "while")) return DYN_TOK_WHILE;
    if (dyn_word(lexer, start, length, "do")) return DYN_TOK_DO;
    if (dyn_word(lexer, start, length, "for")) return DYN_TOK_FOR;
    if (dyn_word(lexer, start, length, "break")) return DYN_TOK_BREAK;
    if (dyn_word(lexer, start, length, "continue")) return DYN_TOK_CONTINUE;
    return DYN_TOK_IDENTIFIER;
}

static unsigned int dyn_escape(char value) {
    if (value == 'n') return 10u;
    if (value == 'r') return 13u;
    if (value == 't') return 9u;
    if (value == '0') return 0u;
    if (value == 'a') return 7u;
    if (value == 'b') return 8u;
    if (value == 'f') return 12u;
    if (value == 'v') return 11u;
    return (unsigned int)value;
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
        dyn_token(
            lexer,
            dyn_keyword(lexer, start, lexer->position - start),
            start,
            0
        );
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
        while (
            lexer->position < lexer->length
            && (lexer->source[lexer->position] == 'u'
                || lexer->source[lexer->position] == 'U'
                || lexer->source[lexer->position] == 'l'
                || lexer->source[lexer->position] == 'L')
        ) lexer->position += 1;
        dyn_token(lexer, DYN_TOK_NUMBER, start, value);
        return;
    }
    if (first == '\'' || first == '"') {
        unsigned int decoded = 0;
        unsigned int count = 0;
        char quote = first;
        lexer->position += 1;
        while (
            lexer->position < lexer->length
            && lexer->source[lexer->position] != quote
        ) {
            unsigned int character;
            if (lexer->source[lexer->position] == '\\') {
                lexer->position += 1;
                if (lexer->position >= lexer->length) break;
                character = dyn_escape(lexer->source[lexer->position]);
            } else character = (unsigned int)lexer->source[lexer->position];
            if (quote == '\'' && count == 0u) decoded = character;
            count += 1u;
            lexer->position += 1;
        }
        if (
            lexer->position >= lexer->length
            || (quote == '\'' && count != 1u)
        ) {
            lexer->error = 1;
            dyn_token(lexer, DYN_TOK_INVALID, start, 0);
            return;
        }
        lexer->position += 1;
        dyn_token(
            lexer,
            quote == '\'' ? DYN_TOK_CHAR : DYN_TOK_STRING,
            start,
            decoded
        );
        return;
    }
    lexer->position += 1;
    if (first == '<' && lexer->position + 1u < lexer->length
        && lexer->source[lexer->position] == '<'
        && lexer->source[lexer->position + 1u] == '=') {
        lexer->position += 2u;
        dyn_token(lexer, DYN_TOK_LSHIFT_ASSIGN, start, 0);
    } else if (first == '>' && lexer->position + 1u < lexer->length
        && lexer->source[lexer->position] == '>'
        && lexer->source[lexer->position + 1u] == '=') {
        lexer->position += 2u;
        dyn_token(lexer, DYN_TOK_RSHIFT_ASSIGN, start, 0);
    } else if (first == '+' && lexer->position < lexer->length
        && lexer->source[lexer->position] == '=') {
        lexer->position += 1u; dyn_token(lexer, DYN_TOK_ADD_ASSIGN, start, 0);
    } else if (first == '-' && lexer->position < lexer->length
        && lexer->source[lexer->position] == '=') {
        lexer->position += 1u; dyn_token(lexer, DYN_TOK_SUB_ASSIGN, start, 0);
    } else if (first == '*' && lexer->position < lexer->length
        && lexer->source[lexer->position] == '=') {
        lexer->position += 1u; dyn_token(lexer, DYN_TOK_MUL_ASSIGN, start, 0);
    } else if (first == '/' && lexer->position < lexer->length
        && lexer->source[lexer->position] == '=') {
        lexer->position += 1u; dyn_token(lexer, DYN_TOK_DIV_ASSIGN, start, 0);
    } else if (first == '%' && lexer->position < lexer->length
        && lexer->source[lexer->position] == '=') {
        lexer->position += 1u; dyn_token(lexer, DYN_TOK_REM_ASSIGN, start, 0);
    } else if (first == '&' && lexer->position < lexer->length
        && lexer->source[lexer->position] == '=') {
        lexer->position += 1u; dyn_token(lexer, DYN_TOK_AND_ASSIGN, start, 0);
    } else if (first == '|' && lexer->position < lexer->length
        && lexer->source[lexer->position] == '=') {
        lexer->position += 1u; dyn_token(lexer, DYN_TOK_OR_ASSIGN, start, 0);
    } else if (first == '^' && lexer->position < lexer->length
        && lexer->source[lexer->position] == '=') {
        lexer->position += 1u; dyn_token(lexer, DYN_TOK_XOR_ASSIGN, start, 0);
    } else if (first == '=' && lexer->position < lexer->length
        && lexer->source[lexer->position] == '=') {
        lexer->position += 1;
        dyn_token(lexer, DYN_TOK_EQUAL, start, 0);
    } else if (first == '!' && lexer->position < lexer->length
        && lexer->source[lexer->position] == '=') {
        lexer->position += 1;
        dyn_token(lexer, DYN_TOK_NOT_EQUAL, start, 0);
    } else if (first == '<' && lexer->position < lexer->length
        && lexer->source[lexer->position] == '=') {
        lexer->position += 1;
        dyn_token(lexer, DYN_TOK_LESS_EQUAL, start, 0);
    } else if (first == '>' && lexer->position < lexer->length
        && lexer->source[lexer->position] == '=') {
        lexer->position += 1;
        dyn_token(lexer, DYN_TOK_GREATER_EQUAL, start, 0);
    } else if (first == '&' && lexer->position < lexer->length
        && lexer->source[lexer->position] == '&') {
        lexer->position += 1;
        dyn_token(lexer, DYN_TOK_LOGICAL_AND, start, 0);
    } else if (first == '|' && lexer->position < lexer->length
        && lexer->source[lexer->position] == '|') {
        lexer->position += 1;
        dyn_token(lexer, DYN_TOK_LOGICAL_OR, start, 0);
    } else if (first == '+' && lexer->position < lexer->length
        && lexer->source[lexer->position] == '+') {
        lexer->position += 1;
        dyn_token(lexer, DYN_TOK_PLUS_PLUS, start, 0);
    } else if (first == '-' && lexer->position < lexer->length
        && lexer->source[lexer->position] == '-') {
        lexer->position += 1;
        dyn_token(lexer, DYN_TOK_MINUS_MINUS, start, 0);
    } else if (first == '-' && lexer->position < lexer->length
        && lexer->source[lexer->position] == '>') {
        lexer->position += 1;
        dyn_token(lexer, DYN_TOK_ARROW, start, 0);
    } else if (first == '<' && lexer->position < lexer->length
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
    } else if (first == '(') dyn_token(lexer, DYN_TOK_LPAREN, start, 0);
    else if (first == ')') dyn_token(lexer, DYN_TOK_RPAREN, start, 0);
    else if (first == '{') dyn_token(lexer, DYN_TOK_LBRACE, start, 0);
    else if (first == '}') dyn_token(lexer, DYN_TOK_RBRACE, start, 0);
    else if (first == '[') dyn_token(lexer, DYN_TOK_LBRACKET, start, 0);
    else if (first == ']') dyn_token(lexer, DYN_TOK_RBRACKET, start, 0);
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
    else if (first == '!') dyn_token(lexer, DYN_TOK_BANG, start, 0);
    else if (first == '?') dyn_token(lexer, DYN_TOK_QUESTION, start, 0);
    else if (first == ':') dyn_token(lexer, DYN_TOK_COLON, start, 0);
    else if (first == ',') dyn_token(lexer, DYN_TOK_COMMA, start, 0);
    else if (first == '.') dyn_token(lexer, DYN_TOK_DOT, start, 0);
    else if (first == '=') dyn_token(lexer, DYN_TOK_ASSIGN, start, 0);
    else if (first == '<') dyn_token(lexer, DYN_TOK_LESS, start, 0);
    else if (first == '>') dyn_token(lexer, DYN_TOK_GREATER, start, 0);
    else {
        lexer->error = 1;
        dyn_token(lexer, DYN_TOK_INVALID, start, 0);
    }
}
