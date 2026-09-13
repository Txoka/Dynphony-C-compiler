#include "dynphony/frontend.h"

static int dyn_lexer_space(char value) {
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

static unsigned int dyn_keyword_key(
    const struct DynLexer *lexer,
    unsigned int start,
    unsigned int length
) {
    unsigned int index = 0;
    unsigned int key = 5381u;
    while (index < length) {
        key = (key << 5u) + key
            + (unsigned int)lexer->source[start + index];
        index += 1u;
    }
    return key;
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
    unsigned int key = dyn_keyword_key(lexer, start, length);
    if (key == 0x0b888030u) return DYN_TOK_INT;
    if (key == 0x7c9faa57u) return DYN_TOK_VOID;
    if (key == 0x7c9a7f6au) return DYN_TOK_MAIN;
    if (key == 0x19306425u) return DYN_TOK_RETURN;
    if (key == 0x9d375962u) return DYN_TOK_UNSIGNED;
    if (key == 0x1bc6ae5fu) return DYN_TOK_SIGNED;
    if (key == 0x7c952063u) return DYN_TOK_CHAR_TYPE;
    if (key == 0x105af0d5u) return DYN_TOK_SHORT;
    if (key == 0x7c9a2f35u) return DYN_TOK_LONG;
    if (key == 0x1c93e1aau) return DYN_TOK_STRUCT;
    if (key == 0x7c96553au) return DYN_TOK_ENUM;
    if (key == 0x07872a76u) return DYN_TOK_TYPEDEF;
    if (key == 0x1c8a8badu) return DYN_TOK_STATIC;
    if (key == 0xfc34e17bu) return DYN_TOK_EXTERN;
    if (key == 0x0f3d3b4cu) return DYN_TOK_CONST;
    if (key == 0x1bd0f495u) return DYN_TOK_SIZEOF;
    if (key == 0x00597834u) return DYN_TOK_IF;
    if (key == 0x7c964c6eu) return DYN_TOK_ELSE;
    if (key == 0x10a3387eu) return DYN_TOK_WHILE;
    if (key == 0x00597798u) return DYN_TOK_DO;
    if (key == 0x0b88738cu) return DYN_TOK_FOR;
    if (key == 0x0f2c9f4au) return DYN_TOK_BREAK;
    if (key == 0x42aefb8au) return DYN_TOK_CONTINUE;
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
            && dyn_lexer_space(lexer->source[lexer->position])
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
