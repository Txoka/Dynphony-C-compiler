#include "dynphony/frontend.h"

#define DYN_INVALID_NODE 0xffffffffu

struct DynParser {
    struct DynLexer lexer;
    struct DynAstProgram *program;
    int error;
};

static unsigned int dyn_new_node(
    struct DynParser *parser,
    int kind,
    unsigned int value,
    unsigned int left,
    unsigned int right
) {
    unsigned int index;
    struct DynNode *node;
    if (parser->program->count == parser->program->capacity) {
        parser->error = 1;
        return DYN_INVALID_NODE;
    }
    index = parser->program->count;
    parser->program->count += 1;
    node = &parser->program->nodes[index];
    node->kind = kind;
    node->value = value;
    node->left = left;
    node->right = right;
    return index;
}

static int dyn_take(struct DynParser *parser, int kind) {
    if (parser->lexer.current.kind != kind) {
        parser->error = 1;
        return 0;
    }
    dyn_lexer_next(&parser->lexer);
    if (parser->lexer.error) parser->error = 1;
    return 1;
}

static unsigned int dyn_expression(struct DynParser *parser);

static unsigned int dyn_primary(struct DynParser *parser) {
    unsigned int node;
    if (parser->lexer.current.kind == DYN_TOK_NUMBER) {
        node = dyn_new_node(
            parser,
            DYN_NODE_NUMBER,
            parser->lexer.current.value,
            DYN_INVALID_NODE,
            DYN_INVALID_NODE
        );
        dyn_lexer_next(&parser->lexer);
        return node;
    }
    if (parser->lexer.current.kind == DYN_TOK_LPAREN) {
        dyn_lexer_next(&parser->lexer);
        node = dyn_expression(parser);
        dyn_take(parser, DYN_TOK_RPAREN);
        return node;
    }
    parser->error = 1;
    return DYN_INVALID_NODE;
}

static unsigned int dyn_unary(struct DynParser *parser) {
    int token = parser->lexer.current.kind;
    int kind;
    unsigned int operand;
    if (
        token != DYN_TOK_PLUS
        && token != DYN_TOK_MINUS
        && token != DYN_TOK_TILDE
    ) return dyn_primary(parser);
    dyn_lexer_next(&parser->lexer);
    operand = dyn_unary(parser);
    kind = DYN_NODE_POSITIVE;
    if (token == DYN_TOK_MINUS) kind = DYN_NODE_NEGATIVE;
    else if (token == DYN_TOK_TILDE) kind = DYN_NODE_NOT;
    return dyn_new_node(
        parser, kind, 0, operand, DYN_INVALID_NODE
    );
}

static int dyn_binary_kind(int token) {
    if (token == DYN_TOK_PLUS) return DYN_NODE_ADD;
    if (token == DYN_TOK_MINUS) return DYN_NODE_SUBTRACT;
    if (token == DYN_TOK_STAR) return DYN_NODE_MULTIPLY;
    if (token == DYN_TOK_SLASH) return DYN_NODE_DIVIDE;
    if (token == DYN_TOK_PERCENT) return DYN_NODE_REMAINDER;
    if (token == DYN_TOK_LSHIFT) return DYN_NODE_LSHIFT;
    if (token == DYN_TOK_RSHIFT) return DYN_NODE_RSHIFT;
    if (token == DYN_TOK_AMP) return DYN_NODE_AND;
    if (token == DYN_TOK_PIPE) return DYN_NODE_OR;
    return DYN_NODE_XOR;
}

static unsigned int dyn_multiplicative(struct DynParser *parser) {
    unsigned int left = dyn_unary(parser);
    while (
        parser->lexer.current.kind == DYN_TOK_STAR
        || parser->lexer.current.kind == DYN_TOK_SLASH
        || parser->lexer.current.kind == DYN_TOK_PERCENT
    ) {
        int token = parser->lexer.current.kind;
        unsigned int right;
        dyn_lexer_next(&parser->lexer);
        right = dyn_unary(parser);
        left = dyn_new_node(parser, dyn_binary_kind(token), 0, left, right);
    }
    return left;
}

static unsigned int dyn_additive(struct DynParser *parser) {
    unsigned int left = dyn_multiplicative(parser);
    while (
        parser->lexer.current.kind == DYN_TOK_PLUS
        || parser->lexer.current.kind == DYN_TOK_MINUS
    ) {
        int token = parser->lexer.current.kind;
        unsigned int right;
        dyn_lexer_next(&parser->lexer);
        right = dyn_multiplicative(parser);
        left = dyn_new_node(parser, dyn_binary_kind(token), 0, left, right);
    }
    return left;
}

static unsigned int dyn_shift(struct DynParser *parser) {
    unsigned int left = dyn_additive(parser);
    while (
        parser->lexer.current.kind == DYN_TOK_LSHIFT
        || parser->lexer.current.kind == DYN_TOK_RSHIFT
    ) {
        int token = parser->lexer.current.kind;
        unsigned int right;
        dyn_lexer_next(&parser->lexer);
        right = dyn_additive(parser);
        left = dyn_new_node(parser, dyn_binary_kind(token), 0, left, right);
    }
    return left;
}

static unsigned int dyn_and(struct DynParser *parser) {
    unsigned int left = dyn_shift(parser);
    while (parser->lexer.current.kind == DYN_TOK_AMP) {
        unsigned int right;
        dyn_lexer_next(&parser->lexer);
        right = dyn_shift(parser);
        left = dyn_new_node(parser, DYN_NODE_AND, 0, left, right);
    }
    return left;
}

static unsigned int dyn_xor(struct DynParser *parser) {
    unsigned int left = dyn_and(parser);
    while (parser->lexer.current.kind == DYN_TOK_CARET) {
        unsigned int right;
        dyn_lexer_next(&parser->lexer);
        right = dyn_and(parser);
        left = dyn_new_node(parser, DYN_NODE_XOR, 0, left, right);
    }
    return left;
}

static unsigned int dyn_expression(struct DynParser *parser) {
    unsigned int left = dyn_xor(parser);
    while (parser->lexer.current.kind == DYN_TOK_PIPE) {
        unsigned int right;
        dyn_lexer_next(&parser->lexer);
        right = dyn_xor(parser);
        left = dyn_new_node(parser, DYN_NODE_OR, 0, left, right);
    }
    return left;
}

int dyn_parse(
    const char *source,
    unsigned int length,
    struct DynAstProgram *program
) {
    struct DynParser parser;
    parser.program = program;
    parser.error = 0;
    program->count = 0;
    program->expression = DYN_INVALID_NODE;
    dyn_lexer_init(&parser.lexer, source, length);
    dyn_take(&parser, DYN_TOK_INT);
    dyn_take(&parser, DYN_TOK_MAIN);
    dyn_take(&parser, DYN_TOK_LPAREN);
    if (parser.lexer.current.kind == DYN_TOK_VOID)
        dyn_lexer_next(&parser.lexer);
    dyn_take(&parser, DYN_TOK_RPAREN);
    dyn_take(&parser, DYN_TOK_LBRACE);
    dyn_take(&parser, DYN_TOK_RETURN);
    program->expression = dyn_expression(&parser);
    dyn_take(&parser, DYN_TOK_SEMICOLON);
    dyn_take(&parser, DYN_TOK_RBRACE);
    if (parser.lexer.current.kind != DYN_TOK_EOF) parser.error = 1;
    return parser.error ? 0 : 1;
}
