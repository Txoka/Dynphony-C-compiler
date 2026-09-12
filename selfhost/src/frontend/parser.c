#include "dynphony/frontend.h"

#define DYN_INVALID_NODE 0xffffffffu

struct DynParser {
    struct DynLexer lexer;
    struct DynAstProgram *program;
    int error;
};

static void dyn_restore_lexer(
    struct DynLexer *destination,
    const struct DynLexer *source
) {
    destination->source = source->source;
    destination->length = source->length;
    destination->position = source->position;
    destination->current.kind = source->current.kind;
    destination->current.value = source->current.value;
    destination->current.position = source->current.position;
    destination->current.length = source->current.length;
    destination->error = source->error;
}

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
    node->extra = DYN_INVALID_NODE;
    return index;
}

static int dyn_same_name(
    const struct DynParser *parser,
    unsigned int left_position,
    unsigned int left_length,
    unsigned int right_position,
    unsigned int right_length
) {
    unsigned int index = 0;
    if (left_length != right_length) return 0;
    while (index < left_length) {
        if (
            parser->program->source[left_position + index]
            != parser->program->source[right_position + index]
        ) return 0;
        index += 1u;
    }
    return 1;
}

static int dyn_token_word(const struct DynParser *parser, const char *word) {
    unsigned int index = 0;
    const struct DynToken *token = &parser->lexer.current;
    while (word[index]) {
        if (
            index >= token->length
            || parser->program->source[token->position + index] != word[index]
        ) return 0;
        index += 1u;
    }
    return index == token->length;
}

static unsigned int dyn_find_local(
    const struct DynParser *parser,
    unsigned int position,
    unsigned int length
) {
    unsigned int index = parser->program->local_count;
    while (index) {
        const struct DynLocal *local;
        index -= 1u;
        local = &parser->program->locals[index];
        if (dyn_same_name(
            parser, local->position, local->length, position, length
        )) return index;
    }
    return DYN_INVALID_NODE;
}

static unsigned int dyn_add_local(struct DynParser *parser, unsigned int size) {
    unsigned int index;
    struct DynLocal *local;
    if (parser->program->local_count >= parser->program->local_capacity) {
        parser->error = 1;
        return DYN_INVALID_NODE;
    }
    index = parser->program->local_count;
    local = &parser->program->locals[index];
    local->position = parser->lexer.current.position;
    local->length = parser->lexer.current.length;
    local->size = size;
    if (dyn_find_local(
        parser, local->position, local->length
    ) != DYN_INVALID_NODE) {
        parser->error = 1;
        return DYN_INVALID_NODE;
    }
    parser->program->local_count += 1u;
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
static unsigned int dyn_assignment(struct DynParser *parser);

static unsigned int dyn_primary(struct DynParser *parser) {
    unsigned int node;
    if (
        parser->lexer.current.kind == DYN_TOK_NUMBER
        || parser->lexer.current.kind == DYN_TOK_CHAR
    ) {
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
    if (parser->lexer.current.kind == DYN_TOK_IDENTIFIER) {
        unsigned int position = parser->lexer.current.position;
        unsigned int length = parser->lexer.current.length;
        unsigned int local;
        if (dyn_token_word(parser, "input")) {
            dyn_lexer_next(&parser->lexer);
            dyn_take(parser, DYN_TOK_LPAREN);
            dyn_take(parser, DYN_TOK_RPAREN);
            return dyn_new_node(
                parser, DYN_NODE_CALL_INPUT, 0,
                DYN_INVALID_NODE, DYN_INVALID_NODE
            );
        }
        if (dyn_token_word(parser, "output")) {
            dyn_lexer_next(&parser->lexer);
            dyn_take(parser, DYN_TOK_LPAREN);
            node = dyn_expression(parser);
            dyn_take(parser, DYN_TOK_RPAREN);
            return dyn_new_node(
                parser, DYN_NODE_CALL_OUTPUT, 0,
                node, DYN_INVALID_NODE
            );
        }
        if (dyn_token_word(parser, "keyboard")
            || dyn_token_word(parser, "time")
            || dyn_token_word(parser, "time_low")
            || dyn_token_word(parser, "time_high")
            || dyn_token_word(parser, "screen")
            || dyn_token_word(parser, "persistent_load")
            || dyn_token_word(parser, "persistent_store")) {
            unsigned int intrinsic = 0;
            unsigned int first_argument = DYN_INVALID_NODE;
            unsigned int second_argument = DYN_INVALID_NODE;
            if (dyn_token_word(parser, "keyboard")) intrinsic = 1u;
            else if (dyn_token_word(parser, "time")
                || dyn_token_word(parser, "time_low")) intrinsic = 2u;
            else if (dyn_token_word(parser, "time_high")) intrinsic = 3u;
            else if (dyn_token_word(parser, "screen")) intrinsic = 4u;
            else if (dyn_token_word(parser, "persistent_load")) intrinsic = 5u;
            else intrinsic = 6u;
            dyn_lexer_next(&parser->lexer);
            dyn_take(parser, DYN_TOK_LPAREN);
            if (intrinsic >= 4u) {
                first_argument = dyn_assignment(parser);
                if (intrinsic == 4u || intrinsic == 6u) {
                    dyn_take(parser, DYN_TOK_COMMA);
                    second_argument = dyn_assignment(parser);
                }
            }
            dyn_take(parser, DYN_TOK_RPAREN);
            return dyn_new_node(
                parser, DYN_NODE_CALL_INTRINSIC, intrinsic,
                first_argument, second_argument
            );
        }
        dyn_lexer_next(&parser->lexer);
        local = dyn_find_local(parser, position, length);
        if (local == DYN_INVALID_NODE) {
            parser->error = 1;
            return DYN_INVALID_NODE;
        }
        return dyn_new_node(
            parser, DYN_NODE_LOCAL, local,
            DYN_INVALID_NODE, DYN_INVALID_NODE
        );
    }
    parser->error = 1;
    return DYN_INVALID_NODE;
}

static unsigned int dyn_increment(
    struct DynParser *parser,
    unsigned int operand,
    int increment
) {
    unsigned int one;
    unsigned int operation;
    if (
        operand == DYN_INVALID_NODE
        || parser->program->nodes[operand].kind != DYN_NODE_LOCAL
    ) {
        parser->error = 1;
        return DYN_INVALID_NODE;
    }
    one = dyn_new_node(
        parser, DYN_NODE_NUMBER, 1u, DYN_INVALID_NODE, DYN_INVALID_NODE
    );
    operation = dyn_new_node(
        parser,
        increment ? DYN_NODE_ADD : DYN_NODE_SUBTRACT,
        0,
        operand,
        one
    );
    return dyn_new_node(parser, DYN_NODE_ASSIGN, 0, operand, operation);
}

static unsigned int dyn_scalar_type(struct DynParser *parser) {
    unsigned int size = 4u;
    if (parser->lexer.current.kind == DYN_TOK_CONST)
        dyn_lexer_next(&parser->lexer);
    if (parser->lexer.current.kind == DYN_TOK_CHAR_TYPE) {
        dyn_lexer_next(&parser->lexer);
        return 1u;
    }
    if (
        parser->lexer.current.kind == DYN_TOK_UNSIGNED
        || parser->lexer.current.kind == DYN_TOK_SIGNED
    ) {
        dyn_lexer_next(&parser->lexer);
        if (
            parser->lexer.current.kind == DYN_TOK_CHAR_TYPE
            || parser->lexer.current.kind == DYN_TOK_SHORT
            || parser->lexer.current.kind == DYN_TOK_LONG
            || parser->lexer.current.kind == DYN_TOK_INT
        ) {
            if (parser->lexer.current.kind == DYN_TOK_CHAR_TYPE) size = 1u;
            else if (parser->lexer.current.kind == DYN_TOK_SHORT) size = 2u;
            dyn_lexer_next(&parser->lexer);
        }
        return size;
    }
    if (parser->lexer.current.kind == DYN_TOK_SHORT) {
        dyn_lexer_next(&parser->lexer);
        if (parser->lexer.current.kind == DYN_TOK_INT)
            dyn_lexer_next(&parser->lexer);
        return 2u;
    }
    if (
        parser->lexer.current.kind == DYN_TOK_INT
        || parser->lexer.current.kind == DYN_TOK_LONG
        || parser->lexer.current.kind == DYN_TOK_VOID
    ) {
        dyn_lexer_next(&parser->lexer);
        return size;
    }
    return 0u;
}

static unsigned int dyn_postfix(struct DynParser *parser) {
    unsigned int operand = dyn_primary(parser);
    while (
        parser->lexer.current.kind == DYN_TOK_PLUS_PLUS
        || parser->lexer.current.kind == DYN_TOK_MINUS_MINUS
    ) {
        int increment = parser->lexer.current.kind == DYN_TOK_PLUS_PLUS;
        dyn_lexer_next(&parser->lexer);
        operand = dyn_increment(parser, operand, increment);
    }
    return operand;
}

static unsigned int dyn_unary(struct DynParser *parser) {
    int token = parser->lexer.current.kind;
    int kind;
    unsigned int operand;
    if (token == DYN_TOK_SIZEOF) {
        struct DynLexer saved;
        unsigned int size;
        dyn_lexer_next(&parser->lexer);
        if (parser->lexer.current.kind == DYN_TOK_LPAREN) {
            dyn_restore_lexer(&saved, &parser->lexer);
            dyn_lexer_next(&parser->lexer);
            size = dyn_scalar_type(parser);
            while (size && parser->lexer.current.kind == DYN_TOK_STAR) {
                size = 4u;
                dyn_lexer_next(&parser->lexer);
            }
            if (size && parser->lexer.current.kind == DYN_TOK_RPAREN) {
                dyn_lexer_next(&parser->lexer);
                return dyn_new_node(
                    parser, DYN_NODE_NUMBER, size,
                    DYN_INVALID_NODE, DYN_INVALID_NODE
                );
            }
            dyn_restore_lexer(&parser->lexer, &saved);
        }
        operand = dyn_unary(parser);
        if (
            operand < parser->program->count
            && parser->program->nodes[operand].kind == DYN_NODE_LOCAL
        ) size = parser->program->locals[
            parser->program->nodes[operand].value
        ].size;
        else size = 4u;
        return dyn_new_node(
            parser, DYN_NODE_NUMBER, size,
            DYN_INVALID_NODE, DYN_INVALID_NODE
        );
    }
    if (token == DYN_TOK_LPAREN) {
        struct DynLexer saved;
        unsigned int size;
        dyn_restore_lexer(&saved, &parser->lexer);
        dyn_lexer_next(&parser->lexer);
        size = dyn_scalar_type(parser);
        while (size && parser->lexer.current.kind == DYN_TOK_STAR)
            dyn_lexer_next(&parser->lexer);
        if (size && parser->lexer.current.kind == DYN_TOK_RPAREN) {
            dyn_lexer_next(&parser->lexer);
            return dyn_unary(parser);
        }
        dyn_restore_lexer(&parser->lexer, &saved);
    }
    if (
        token != DYN_TOK_PLUS
        && token != DYN_TOK_MINUS
        && token != DYN_TOK_TILDE
        && token != DYN_TOK_BANG
        && token != DYN_TOK_PLUS_PLUS
        && token != DYN_TOK_MINUS_MINUS
    ) return dyn_postfix(parser);
    dyn_lexer_next(&parser->lexer);
    operand = dyn_unary(parser);
    if (token == DYN_TOK_PLUS_PLUS || token == DYN_TOK_MINUS_MINUS)
        return dyn_increment(parser, operand, token == DYN_TOK_PLUS_PLUS);
    kind = DYN_NODE_POSITIVE;
    if (token == DYN_TOK_MINUS) kind = DYN_NODE_NEGATIVE;
    else if (token == DYN_TOK_TILDE) kind = DYN_NODE_NOT;
    else if (token == DYN_TOK_BANG) kind = DYN_NODE_LOGICAL_NOT;
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

static unsigned int dyn_relational(struct DynParser *parser) {
    unsigned int left = dyn_shift(parser);
    while (
        parser->lexer.current.kind == DYN_TOK_LESS
        || parser->lexer.current.kind == DYN_TOK_GREATER
        || parser->lexer.current.kind == DYN_TOK_LESS_EQUAL
        || parser->lexer.current.kind == DYN_TOK_GREATER_EQUAL
    ) {
        int token = parser->lexer.current.kind;
        int kind = DYN_NODE_LESS;
        unsigned int right;
        dyn_lexer_next(&parser->lexer);
        right = dyn_shift(parser);
        if (token == DYN_TOK_GREATER) kind = DYN_NODE_GREATER;
        else if (token == DYN_TOK_LESS_EQUAL) kind = DYN_NODE_LESS_EQUAL;
        else if (token == DYN_TOK_GREATER_EQUAL) kind = DYN_NODE_GREATER_EQUAL;
        left = dyn_new_node(parser, kind, 0, left, right);
    }
    return left;
}

static unsigned int dyn_equality(struct DynParser *parser) {
    unsigned int left = dyn_relational(parser);
    while (
        parser->lexer.current.kind == DYN_TOK_EQUAL
        || parser->lexer.current.kind == DYN_TOK_NOT_EQUAL
    ) {
        int token = parser->lexer.current.kind;
        unsigned int right;
        dyn_lexer_next(&parser->lexer);
        right = dyn_relational(parser);
        left = dyn_new_node(
            parser,
            token == DYN_TOK_EQUAL ? DYN_NODE_EQUAL : DYN_NODE_NOT_EQUAL,
            0,
            left,
            right
        );
    }
    return left;
}

static unsigned int dyn_and(struct DynParser *parser) {
    unsigned int left = dyn_equality(parser);
    while (parser->lexer.current.kind == DYN_TOK_AMP) {
        unsigned int right;
        dyn_lexer_next(&parser->lexer);
        right = dyn_equality(parser);
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

static unsigned int dyn_bitwise_or(struct DynParser *parser) {
    unsigned int left = dyn_xor(parser);
    while (parser->lexer.current.kind == DYN_TOK_PIPE) {
        unsigned int right;
        dyn_lexer_next(&parser->lexer);
        right = dyn_xor(parser);
        left = dyn_new_node(parser, DYN_NODE_OR, 0, left, right);
    }
    return left;
}

static unsigned int dyn_logical_and(struct DynParser *parser) {
    unsigned int left = dyn_bitwise_or(parser);
    while (parser->lexer.current.kind == DYN_TOK_LOGICAL_AND) {
        unsigned int right;
        dyn_lexer_next(&parser->lexer);
        right = dyn_bitwise_or(parser);
        left = dyn_new_node(parser, DYN_NODE_LOGICAL_AND, 0, left, right);
    }
    return left;
}

static unsigned int dyn_logical_or(struct DynParser *parser) {
    unsigned int left = dyn_logical_and(parser);
    while (parser->lexer.current.kind == DYN_TOK_LOGICAL_OR) {
        unsigned int right;
        dyn_lexer_next(&parser->lexer);
        right = dyn_logical_and(parser);
        left = dyn_new_node(parser, DYN_NODE_LOGICAL_OR, 0, left, right);
    }
    return left;
}

static unsigned int dyn_conditional(struct DynParser *parser) {
    unsigned int condition = dyn_logical_or(parser);
    if (parser->lexer.current.kind == DYN_TOK_QUESTION) {
        unsigned int if_true;
        unsigned int if_false;
        dyn_lexer_next(&parser->lexer);
        if_true = dyn_expression(parser);
        dyn_take(parser, DYN_TOK_COLON);
        if_false = dyn_conditional(parser);
        return dyn_new_node(
            parser, DYN_NODE_CONDITIONAL, condition, if_true, if_false
        );
    }
    return condition;
}

static unsigned int dyn_assignment(struct DynParser *parser) {
    unsigned int left = dyn_conditional(parser);
    int token = parser->lexer.current.kind;
    if (
        token == DYN_TOK_ASSIGN || token == DYN_TOK_ADD_ASSIGN
        || token == DYN_TOK_SUB_ASSIGN || token == DYN_TOK_MUL_ASSIGN
        || token == DYN_TOK_DIV_ASSIGN || token == DYN_TOK_REM_ASSIGN
        || token == DYN_TOK_AND_ASSIGN || token == DYN_TOK_OR_ASSIGN
        || token == DYN_TOK_XOR_ASSIGN || token == DYN_TOK_LSHIFT_ASSIGN
        || token == DYN_TOK_RSHIFT_ASSIGN
    ) {
        unsigned int right;
        int operation = DYN_NODE_ADD;
        if (
            left == DYN_INVALID_NODE
            || parser->program->nodes[left].kind != DYN_NODE_LOCAL
        ) parser->error = 1;
        dyn_lexer_next(&parser->lexer);
        right = dyn_assignment(parser);
        if (token != DYN_TOK_ASSIGN) {
            if (token == DYN_TOK_SUB_ASSIGN) operation = DYN_NODE_SUBTRACT;
            else if (token == DYN_TOK_MUL_ASSIGN) operation = DYN_NODE_MULTIPLY;
            else if (token == DYN_TOK_DIV_ASSIGN) operation = DYN_NODE_DIVIDE;
            else if (token == DYN_TOK_REM_ASSIGN) operation = DYN_NODE_REMAINDER;
            else if (token == DYN_TOK_AND_ASSIGN) operation = DYN_NODE_AND;
            else if (token == DYN_TOK_OR_ASSIGN) operation = DYN_NODE_OR;
            else if (token == DYN_TOK_XOR_ASSIGN) operation = DYN_NODE_XOR;
            else if (token == DYN_TOK_LSHIFT_ASSIGN) operation = DYN_NODE_LSHIFT;
            else if (token == DYN_TOK_RSHIFT_ASSIGN) operation = DYN_NODE_RSHIFT;
            right = dyn_new_node(parser, operation, 0, left, right);
        }
        return dyn_new_node(parser, DYN_NODE_ASSIGN, 0, left, right);
    }
    return left;
}

static unsigned int dyn_expression(struct DynParser *parser) {
    unsigned int left = dyn_assignment(parser);
    while (parser->lexer.current.kind == DYN_TOK_COMMA) {
        unsigned int right;
        dyn_lexer_next(&parser->lexer);
        right = dyn_assignment(parser);
        left = dyn_new_node(parser, DYN_NODE_COMMA, 0, left, right);
    }
    return left;
}

static unsigned int dyn_statement(struct DynParser *parser);

static unsigned int dyn_sequence(
    struct DynParser *parser,
    unsigned int left,
    unsigned int right
) {
    if (left == DYN_INVALID_NODE) return right;
    return dyn_new_node(parser, DYN_NODE_SEQUENCE, 0, left, right);
}

static int dyn_declaration_start(int kind) {
    return kind == DYN_TOK_INT || kind == DYN_TOK_UNSIGNED
        || kind == DYN_TOK_SIGNED || kind == DYN_TOK_CHAR_TYPE
        || kind == DYN_TOK_SHORT || kind == DYN_TOK_LONG
        || kind == DYN_TOK_CONST;
}

static unsigned int dyn_declaration(struct DynParser *parser) {
    unsigned int local;
    unsigned int initializer;
    unsigned int size = dyn_scalar_type(parser);
    while (parser->lexer.current.kind == DYN_TOK_STAR) {
        size = 4u;
        dyn_lexer_next(&parser->lexer);
    }
    if (parser->lexer.current.kind != DYN_TOK_IDENTIFIER) {
        parser->error = 1;
        return DYN_INVALID_NODE;
    }
    local = dyn_add_local(parser, size);
    dyn_lexer_next(&parser->lexer);
    if (parser->lexer.current.kind == DYN_TOK_ASSIGN) {
        dyn_lexer_next(&parser->lexer);
        initializer = dyn_assignment(parser);
    } else initializer = dyn_new_node(
        parser, DYN_NODE_NUMBER, 0, DYN_INVALID_NODE, DYN_INVALID_NODE
    );
    dyn_take(parser, DYN_TOK_SEMICOLON);
    return dyn_new_node(
        parser,
        DYN_NODE_ASSIGN,
        0,
        dyn_new_node(
            parser, DYN_NODE_LOCAL, local,
            DYN_INVALID_NODE, DYN_INVALID_NODE
        ),
        initializer
    );
}

static unsigned int dyn_statement(struct DynParser *parser) {
    unsigned int node;
    if (parser->lexer.current.kind == DYN_TOK_LBRACE) {
        unsigned int sequence = DYN_INVALID_NODE;
        dyn_lexer_next(&parser->lexer);
        while (
            parser->lexer.current.kind != DYN_TOK_RBRACE
            && parser->lexer.current.kind != DYN_TOK_EOF
            && !parser->error
        ) sequence = dyn_sequence(parser, sequence, dyn_statement(parser));
        dyn_take(parser, DYN_TOK_RBRACE);
        return sequence;
    }
    if (dyn_declaration_start(parser->lexer.current.kind))
        return dyn_declaration(parser);
    if (parser->lexer.current.kind == DYN_TOK_RETURN) {
        dyn_lexer_next(&parser->lexer);
        node = dyn_expression(parser);
        dyn_take(parser, DYN_TOK_SEMICOLON);
        return dyn_new_node(
            parser, DYN_NODE_RETURN, 0, node, DYN_INVALID_NODE
        );
    }
    if (parser->lexer.current.kind == DYN_TOK_IF) {
        unsigned int condition;
        unsigned int if_true;
        unsigned int if_false = DYN_INVALID_NODE;
        dyn_lexer_next(&parser->lexer);
        dyn_take(parser, DYN_TOK_LPAREN);
        condition = dyn_expression(parser);
        dyn_take(parser, DYN_TOK_RPAREN);
        if_true = dyn_statement(parser);
        if (parser->lexer.current.kind == DYN_TOK_ELSE) {
            dyn_lexer_next(&parser->lexer);
            if_false = dyn_statement(parser);
        }
        node = dyn_new_node(parser, DYN_NODE_IF, condition, if_true, if_false);
        return node;
    }
    if (parser->lexer.current.kind == DYN_TOK_WHILE) {
        unsigned int condition;
        unsigned int body;
        dyn_lexer_next(&parser->lexer);
        dyn_take(parser, DYN_TOK_LPAREN);
        condition = dyn_expression(parser);
        dyn_take(parser, DYN_TOK_RPAREN);
        body = dyn_statement(parser);
        return dyn_new_node(
            parser, DYN_NODE_WHILE, 0, condition, body
        );
    }
    if (parser->lexer.current.kind == DYN_TOK_DO) {
        unsigned int body;
        unsigned int condition;
        dyn_lexer_next(&parser->lexer);
        body = dyn_statement(parser);
        dyn_take(parser, DYN_TOK_WHILE);
        dyn_take(parser, DYN_TOK_LPAREN);
        condition = dyn_expression(parser);
        dyn_take(parser, DYN_TOK_RPAREN);
        dyn_take(parser, DYN_TOK_SEMICOLON);
        return dyn_new_node(parser, DYN_NODE_DO, 0, body, condition);
    }
    if (parser->lexer.current.kind == DYN_TOK_FOR) {
        unsigned int initializer = DYN_INVALID_NODE;
        unsigned int condition;
        unsigned int increment = DYN_INVALID_NODE;
        unsigned int body;
        dyn_lexer_next(&parser->lexer);
        dyn_take(parser, DYN_TOK_LPAREN);
        if (dyn_declaration_start(parser->lexer.current.kind))
            initializer = dyn_declaration(parser);
        else {
            if (parser->lexer.current.kind != DYN_TOK_SEMICOLON)
                initializer = dyn_expression(parser);
            dyn_take(parser, DYN_TOK_SEMICOLON);
        }
        if (parser->lexer.current.kind != DYN_TOK_SEMICOLON)
            condition = dyn_expression(parser);
        else condition = dyn_new_node(
            parser, DYN_NODE_NUMBER, 1u,
            DYN_INVALID_NODE, DYN_INVALID_NODE
        );
        dyn_take(parser, DYN_TOK_SEMICOLON);
        if (parser->lexer.current.kind != DYN_TOK_RPAREN)
            increment = dyn_expression(parser);
        dyn_take(parser, DYN_TOK_RPAREN);
        body = dyn_statement(parser);
        node = dyn_new_node(parser, DYN_NODE_FOR, initializer, condition, body);
        if (node != DYN_INVALID_NODE) parser->program->nodes[node].extra = increment;
        return node;
    }
    if (parser->lexer.current.kind == DYN_TOK_BREAK) {
        dyn_lexer_next(&parser->lexer);
        dyn_take(parser, DYN_TOK_SEMICOLON);
        return dyn_new_node(
            parser, DYN_NODE_BREAK, 0, DYN_INVALID_NODE, DYN_INVALID_NODE
        );
    }
    if (parser->lexer.current.kind == DYN_TOK_CONTINUE) {
        dyn_lexer_next(&parser->lexer);
        dyn_take(parser, DYN_TOK_SEMICOLON);
        return dyn_new_node(
            parser, DYN_NODE_CONTINUE, 0, DYN_INVALID_NODE, DYN_INVALID_NODE
        );
    }
    node = dyn_expression(parser);
    dyn_take(parser, DYN_TOK_SEMICOLON);
    return dyn_new_node(
        parser, DYN_NODE_EXPRESSION, 0, node, DYN_INVALID_NODE
    );
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
    program->local_count = 0;
    program->source = source;
    program->expression = DYN_INVALID_NODE;
    dyn_lexer_init(&parser.lexer, source, length);
    dyn_take(&parser, DYN_TOK_INT);
    dyn_take(&parser, DYN_TOK_MAIN);
    dyn_take(&parser, DYN_TOK_LPAREN);
    if (parser.lexer.current.kind == DYN_TOK_VOID)
        dyn_lexer_next(&parser.lexer);
    dyn_take(&parser, DYN_TOK_RPAREN);
    program->expression = dyn_statement(&parser);
    if (parser.lexer.current.kind != DYN_TOK_EOF) parser.error = 1;
    return parser.error ? 0 : 1;
}
