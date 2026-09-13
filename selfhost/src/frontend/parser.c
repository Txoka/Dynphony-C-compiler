#include <stdlib.h>
#include "dynphony/frontend.h"

#define DYN_INVALID_NODE 0xffffffffu

struct DynParser {
    struct DynLexer lexer;
    struct DynAstProgram *program;
    int error;
    unsigned int local_base;
    unsigned int frame_size;
    unsigned int scope_depth;
    unsigned int type_struct;
    unsigned int type_element_size;
    int type_pointer;
};

struct DynParsedDimensions {
    unsigned int start;
    unsigned int count;
    unsigned int total;
    unsigned int total_node;
    int variable;
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
    node->dimension_start = 0u;
    node->dimension_count = 0u;
    node->stride_node = DYN_INVALID_NODE;
    if (kind == DYN_NODE_LOCAL && value < parser->program->local_count) {
        node->dimension_start = parser->program->locals[value].dimension_start;
        node->dimension_count = parser->program->locals[value].dimension_count;
    } else if (kind == DYN_NODE_GLOBAL
        && value < parser->program->global_count) {
        node->dimension_start = parser->program->globals[value].dimension_start;
        node->dimension_count = parser->program->globals[value].dimension_count;
    }
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

static int dyn_token_call(struct DynParser *parser) {
    struct DynLexer saved;
    int call;
    dyn_restore_lexer(&saved, &parser->lexer);
    dyn_lexer_next(&parser->lexer);
    call = parser->lexer.current.kind == DYN_TOK_LPAREN;
    dyn_restore_lexer(&parser->lexer, &saved);
    return call;
}

static unsigned int dyn_find_local_from(
    const struct DynParser *parser,
    unsigned int position,
    unsigned int length,
    unsigned int index
) {
    const struct DynLocal *local;
    if (index <= parser->local_base) return DYN_INVALID_NODE;
    index -= 1u;
    local = &parser->program->locals[index];
    if (local->active && local->scope_depth <= parser->scope_depth && dyn_same_name(
        parser, local->position, local->length, position, length
    )) return index;
    return dyn_find_local_from(parser, position, length, index);
}

static unsigned int dyn_find_local(
    const struct DynParser *parser,
    unsigned int position,
    unsigned int length
) {
    return dyn_find_local_from(
        parser, position, length, parser->program->local_count
    );
}

static unsigned int dyn_find_constant_from(
    const struct DynParser *parser,
    unsigned int position,
    unsigned int length,
    unsigned int index
) {
    const struct DynConstant *constant;
    if (!index) return DYN_INVALID_NODE;
    index -= 1u;
    constant = &parser->program->constants[index];
    if (constant->active && dyn_same_name(
        parser, constant->name_position, constant->name_length, position, length
    )) return index;
    return dyn_find_constant_from(parser, position, length, index);
}

static unsigned int dyn_find_constant(
    const struct DynParser *parser,
    unsigned int position,
    unsigned int length
) {
    return dyn_find_constant_from(
        parser, position, length, parser->program->constant_count
    );
}

static unsigned int dyn_find_alias_from(
    const struct DynParser *parser,
    unsigned int position,
    unsigned int length,
    unsigned int index
) {
    const struct DynTypeAlias *alias;
    if (!index) return DYN_INVALID_NODE;
    index -= 1u;
    alias = &parser->program->aliases[index];
    if (alias->active && dyn_same_name(
        parser, alias->name_position, alias->name_length, position, length
    )) return index;
    return dyn_find_alias_from(parser, position, length, index);
}

static unsigned int dyn_find_alias(
    const struct DynParser *parser,
    unsigned int position,
    unsigned int length
) {
    return dyn_find_alias_from(
        parser, position, length, parser->program->alias_count
    );
}

static unsigned int dyn_find_enum_from(
    const struct DynParser *parser,
    unsigned int position,
    unsigned int length,
    unsigned int index
) {
    const struct DynEnumTag *tag;
    if (!index) return DYN_INVALID_NODE;
    index -= 1u;
    tag = &parser->program->enums[index];
    if (tag->active && dyn_same_name(
        parser, tag->name_position, tag->name_length, position, length
    )) return index;
    return dyn_find_enum_from(parser, position, length, index);
}

static unsigned int dyn_find_enum(
    const struct DynParser *parser,
    unsigned int position,
    unsigned int length
) {
    return dyn_find_enum_from(
        parser, position, length, parser->program->enum_count
    );
}

static unsigned int dyn_find_struct_from(
    const struct DynParser *parser,
    unsigned int position,
    unsigned int length,
    unsigned int index
) {
    const struct DynStruct *structure;
    if (!index) return DYN_INVALID_NODE;
    index -= 1u;
    structure = &parser->program->structs[index];
    if (dyn_same_name(
        parser, structure->name_position, structure->name_length,
        position, length
    )) return index;
    return dyn_find_struct_from(parser, position, length, index);
}

static unsigned int dyn_find_struct(
    const struct DynParser *parser,
    unsigned int position,
    unsigned int length
) {
    return dyn_find_struct_from(
        parser, position, length, parser->program->struct_count
    );
}

static unsigned int dyn_find_global_from(
    const struct DynParser *parser,
    unsigned int position,
    unsigned int length,
    unsigned int index
) {
    const struct DynGlobal *global;
    if (!index) return DYN_INVALID_NODE;
    index -= 1u;
    global = &parser->program->globals[index];
    if (global->defined && dyn_same_name(
        parser, global->name_position, global->name_length, position, length
    )) return index;
    return dyn_find_global_from(parser, position, length, index);
}

static unsigned int dyn_find_global(
    const struct DynParser *parser,
    unsigned int position,
    unsigned int length
) {
    return dyn_find_global_from(
        parser, position, length, parser->program->global_count
    );
}

static unsigned int dyn_find_member(
    const struct DynParser *parser,
    unsigned int structure,
    unsigned int position,
    unsigned int length
) {
    const struct DynStruct *type;
    unsigned int index;
    if (structure >= parser->program->struct_count) return DYN_INVALID_NODE;
    type = &parser->program->structs[structure];
    index = type->member_start;
    while (index < type->member_start + type->member_count) {
        const struct DynMember *member = &parser->program->members[index];
        if (dyn_same_name(
            parser, member->name_position, member->name_length,
            position, length
        )) return index;
        index += 1u;
    }
    return DYN_INVALID_NODE;
}

static unsigned int dyn_add_local(
    struct DynParser *parser,
    unsigned int size,
    int pointer
) {
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
    local->element_size = size;
    local->count = 1u;
    local->array = 0;
    local->pointer = pointer;
    local->struct_id = parser->type_struct;
    local->dimension_start = 0u;
    local->dimension_count = 0u;
    local->total_size_node = DYN_INVALID_NODE;
    local->vla = 0;
    local->scope_depth = parser->scope_depth;
    local->active = 1;
    local->static_storage = 0;
    local->static_global = DYN_INVALID_NODE;
    if (parser->type_struct != DYN_INVALID_NODE && !pointer)
        parser->frame_size += (size + 3u) & 0xfffffffcu;
    else parser->frame_size += 4u;
    local->offset = parser->frame_size;
    {
        unsigned int previous = dyn_find_local(
            parser, local->position, local->length
        );
        if (previous != DYN_INVALID_NODE
            && parser->program->locals[previous].scope_depth
                == parser->scope_depth) {
            parser->error = 1;
            return DYN_INVALID_NODE;
        }
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

static unsigned int dyn_string_escape(char value) {
    if (value == 'n') return 10u;
    if (value == 'r') return 13u;
    if (value == 't') return 9u;
    if (value == 'a') return 7u;
    if (value == 'b') return 8u;
    if (value == 'f') return 12u;
    if (value == 'v') return 11u;
    return (unsigned int)value;
}

static unsigned int dyn_parse_string(struct DynParser *parser) {
    struct DynGlobal *global;
    unsigned int global_index;
    unsigned int capacity = 1u;
    unsigned int length = 0;
    struct DynLexer saved;
    char *data;
    dyn_restore_lexer(&saved, &parser->lexer);
    while (parser->lexer.current.kind == DYN_TOK_STRING) {
        capacity += parser->lexer.current.length;
        dyn_lexer_next(&parser->lexer);
    }
    data = malloc(capacity);
    if (!data || parser->program->global_count >= parser->program->global_capacity) {
        if (data) free(data);
        parser->error = 1;
        return DYN_INVALID_NODE;
    }
    dyn_restore_lexer(&parser->lexer, &saved);
    while (parser->lexer.current.kind == DYN_TOK_STRING) {
        unsigned int position = parser->lexer.current.position + 1u;
        unsigned int end = parser->lexer.current.position
            + parser->lexer.current.length - 1u;
        while (position < end) {
            unsigned int value;
            if (parser->program->source[position] == '\\') {
                position += 1u;
                value = dyn_string_escape(parser->program->source[position]);
            } else value = (unsigned int)parser->program->source[position];
            data[length] = (char)(value & 255u);
            length += 1u;
            position += 1u;
        }
        dyn_lexer_next(&parser->lexer);
    }
    data[length] = 0;
    length += 1u;
    global_index = parser->program->global_count;
    parser->program->global_count += 1u;
    global = &parser->program->globals[global_index];
    global->name_position = 0;
    global->name_length = 0;
    global->size = 1u;
    global->element_size = 1u;
    global->count = length;
    global->initial_value = 0;
    global->initializer_node = DYN_INVALID_NODE;
    global->array = 1;
    global->pointer = 0;
    global->struct_id = DYN_INVALID_NODE;
    global->dimension_start = 0u;
    global->dimension_count = 0u;
    global->defined = 1;
    global->internal = 1;
    global->data = data;
    global->data_length = length;
    return dyn_new_node(
        parser, DYN_NODE_GLOBAL, global_index, DYN_INVALID_NODE, 0u
    );
}

static unsigned int dyn_primary(struct DynParser *parser) {
    unsigned int node;
    if (parser->lexer.current.kind == DYN_TOK_STRING)
        return dyn_parse_string(parser);
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
    if (
        parser->lexer.current.kind == DYN_TOK_IDENTIFIER
        || parser->lexer.current.kind == DYN_TOK_MAIN
    ) {
        unsigned int position = parser->lexer.current.position;
        unsigned int length = parser->lexer.current.length;
        unsigned int local;
        if (dyn_token_word(parser, "input") && dyn_token_call(parser)) {
            dyn_lexer_next(&parser->lexer);
            dyn_take(parser, DYN_TOK_LPAREN);
            dyn_take(parser, DYN_TOK_RPAREN);
            return dyn_new_node(
                parser, DYN_NODE_CALL_INPUT, 0,
                DYN_INVALID_NODE, DYN_INVALID_NODE
            );
        }
        if (dyn_token_word(parser, "output") && dyn_token_call(parser)) {
            dyn_lexer_next(&parser->lexer);
            dyn_take(parser, DYN_TOK_LPAREN);
            node = dyn_expression(parser);
            dyn_take(parser, DYN_TOK_RPAREN);
            return dyn_new_node(
                parser, DYN_NODE_CALL_OUTPUT, 0,
                node, DYN_INVALID_NODE
            );
        }
        if (dyn_token_call(parser) && (dyn_token_word(parser, "keyboard")
            || dyn_token_word(parser, "time")
            || dyn_token_word(parser, "time_low")
            || dyn_token_word(parser, "time_high")
            || dyn_token_word(parser, "screen")
            || dyn_token_word(parser, "persistent_load")
            || dyn_token_word(parser, "persistent_store")
            || dyn_token_word(parser, "dynphony_heap_remaining"))) {
            unsigned int intrinsic = 0;
            unsigned int first_argument = DYN_INVALID_NODE;
            unsigned int second_argument = DYN_INVALID_NODE;
            if (dyn_token_word(parser, "keyboard")) intrinsic = 1u;
            else if (dyn_token_word(parser, "time")
                || dyn_token_word(parser, "time_low")) intrinsic = 2u;
            else if (dyn_token_word(parser, "time_high")) intrinsic = 3u;
            else if (dyn_token_word(parser, "screen")) intrinsic = 4u;
            else if (dyn_token_word(parser, "persistent_load")) intrinsic = 5u;
            else if (dyn_token_word(parser, "persistent_store")) intrinsic = 6u;
            else intrinsic = 7u;
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
        if (parser->lexer.current.kind == DYN_TOK_LPAREN) {
            unsigned int arguments = DYN_INVALID_NODE;
            unsigned int *tail = &arguments;
            dyn_lexer_next(&parser->lexer);
            while (
                parser->lexer.current.kind != DYN_TOK_RPAREN
                && !parser->error
            ) {
                unsigned int argument = dyn_assignment(parser);
                unsigned int item = dyn_new_node(
                    parser, DYN_NODE_ARGUMENT, 0,
                    argument, DYN_INVALID_NODE
                );
                *tail = item;
                tail = &parser->program->nodes[item].right;
                if (parser->lexer.current.kind != DYN_TOK_COMMA) break;
                dyn_lexer_next(&parser->lexer);
            }
            dyn_take(parser, DYN_TOK_RPAREN);
            node = dyn_new_node(
                parser, DYN_NODE_CALL, position,
                arguments, DYN_INVALID_NODE
            );
            if (node != DYN_INVALID_NODE)
                parser->program->nodes[node].extra = length;
            return node;
        }
        local = dyn_find_local(parser, position, length);
        if (local == DYN_INVALID_NODE) {
            unsigned int constant = dyn_find_constant(parser, position, length);
            unsigned int global;
            if (constant != DYN_INVALID_NODE)
                return dyn_new_node(
                    parser, DYN_NODE_NUMBER,
                    parser->program->constants[constant].value,
                    DYN_INVALID_NODE, DYN_INVALID_NODE
                );
            global = dyn_find_global(parser, position, length);
            if (global != DYN_INVALID_NODE) {
                node = dyn_new_node(
                    parser, DYN_NODE_GLOBAL, global,
                    DYN_INVALID_NODE, 0u
                );
                if (node != DYN_INVALID_NODE) {
                    parser->program->nodes[node].dimension_start =
                        parser->program->globals[global].dimension_start;
                    parser->program->nodes[node].dimension_count =
                        parser->program->globals[global].dimension_count;
                }
                return node;
            }
            node = dyn_new_node(
                parser, DYN_NODE_GLOBAL, position,
                DYN_INVALID_NODE, DYN_INVALID_NODE
            );
            if (node != DYN_INVALID_NODE)
                parser->program->nodes[node].extra = length;
            return node;
        }
        node = dyn_new_node(
            parser, DYN_NODE_LOCAL, local,
            DYN_INVALID_NODE, DYN_INVALID_NODE
        );
        if (node != DYN_INVALID_NODE) {
            parser->program->nodes[node].dimension_start =
                parser->program->locals[local].dimension_start;
            parser->program->nodes[node].dimension_count =
                parser->program->locals[local].dimension_count;
        }
        return node;
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
    parser->type_struct = DYN_INVALID_NODE;
    parser->type_element_size = 4u;
    parser->type_pointer = 0;
    if (parser->lexer.current.kind == DYN_TOK_CONST)
        dyn_lexer_next(&parser->lexer);
    if (parser->lexer.current.kind == DYN_TOK_STRUCT) {
        unsigned int structure;
        dyn_lexer_next(&parser->lexer);
        if (parser->lexer.current.kind != DYN_TOK_IDENTIFIER) return 0u;
        structure = dyn_find_struct(
            parser, parser->lexer.current.position,
            parser->lexer.current.length
        );
        if (structure == DYN_INVALID_NODE) return 0u;
        parser->type_struct = structure;
        size = parser->program->structs[structure].size;
        parser->type_element_size = size;
        dyn_lexer_next(&parser->lexer);
        return size;
    }
    if (parser->lexer.current.kind == DYN_TOK_ENUM) {
        dyn_lexer_next(&parser->lexer);
        if (parser->lexer.current.kind != DYN_TOK_IDENTIFIER
            || dyn_find_enum(
                parser, parser->lexer.current.position,
                parser->lexer.current.length
            ) == DYN_INVALID_NODE) return 0u;
        dyn_lexer_next(&parser->lexer);
        parser->type_element_size = 4u;
        return 4u;
    }
    if (parser->lexer.current.kind == DYN_TOK_CHAR_TYPE) {
        dyn_lexer_next(&parser->lexer);
        parser->type_element_size = 1u;
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
        parser->type_element_size = size;
        return size;
    }
    if (parser->lexer.current.kind == DYN_TOK_SHORT) {
        dyn_lexer_next(&parser->lexer);
        if (parser->lexer.current.kind == DYN_TOK_INT)
            dyn_lexer_next(&parser->lexer);
        parser->type_element_size = 2u;
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
    if (parser->lexer.current.kind == DYN_TOK_IDENTIFIER) {
        unsigned int alias = dyn_find_alias(
            parser, parser->lexer.current.position,
            parser->lexer.current.length
        );
        if (alias != DYN_INVALID_NODE) {
            size = parser->program->aliases[alias].size;
            parser->type_element_size =
                parser->program->aliases[alias].element_size;
            parser->type_struct = parser->program->aliases[alias].struct_id;
            parser->type_pointer = parser->program->aliases[alias].pointer;
            dyn_lexer_next(&parser->lexer);
            return size;
        }
    }
    return 0u;
}

static unsigned int dyn_node_struct(
    const struct DynParser *parser,
    unsigned int index
) {
    const struct DynNode *node;
    if (index >= parser->program->count) return DYN_INVALID_NODE;
    node = &parser->program->nodes[index];
    if (node->kind == DYN_NODE_LOCAL)
        return parser->program->locals[node->value].struct_id;
    if (node->kind == DYN_NODE_GLOBAL
        && node->value < parser->program->global_count)
        return parser->program->globals[node->value].struct_id;
    if (node->kind == DYN_NODE_DEREFERENCE
        || node->kind == DYN_NODE_SUBSCRIPT)
        return node->extra;
    if (node->kind == DYN_NODE_MEMBER
        && node->value < parser->program->member_count)
        return parser->program->members[node->value].struct_id;
    if (node->kind == DYN_NODE_ADDRESS)
        return dyn_node_struct(parser, node->left);
    return DYN_INVALID_NODE;
}

static int dyn_node_pointer(
    const struct DynParser *parser,
    unsigned int index
) {
    const struct DynNode *node;
    if (index >= parser->program->count) return 0;
    node = &parser->program->nodes[index];
    if (node->kind == DYN_NODE_LOCAL) {
        const struct DynLocal *local = &parser->program->locals[node->value];
        return local->pointer || local->array;
    }
    if (node->kind == DYN_NODE_GLOBAL
        && node->value < parser->program->global_count) {
        const struct DynGlobal *global = &parser->program->globals[node->value];
        return global->pointer || global->array;
    }
    if (node->kind == DYN_NODE_ADDRESS) return 1;
    if (node->kind == DYN_NODE_SUBSCRIPT)
        return node->dimension_count != 0u;
    if (node->kind == DYN_NODE_MEMBER
        && node->value < parser->program->member_count) {
        const struct DynMember *member = &parser->program->members[node->value];
        return member->pointer || member->array;
    }
    if (node->kind == DYN_NODE_ADD || node->kind == DYN_NODE_SUBTRACT)
        return dyn_node_pointer(parser, node->left)
            || (node->kind == DYN_NODE_ADD
                && dyn_node_pointer(parser, node->right));
    return 0;
}

static unsigned int dyn_postfix(struct DynParser *parser) {
    unsigned int operand = dyn_primary(parser);
    while (parser->lexer.current.kind == DYN_TOK_LBRACKET
        || parser->lexer.current.kind == DYN_TOK_DOT
        || parser->lexer.current.kind == DYN_TOK_ARROW) {
        if (parser->lexer.current.kind == DYN_TOK_LBRACKET) {
            unsigned int subscript;
            unsigned int element_size = 4u;
            unsigned int structure = dyn_node_struct(parser, operand);
            unsigned int node;
            unsigned int dimension_start = 0u;
            unsigned int dimension_count = 0u;
            if (operand < parser->program->count) {
                dimension_start =
                    parser->program->nodes[operand].dimension_start;
                dimension_count =
                    parser->program->nodes[operand].dimension_count;
            }
            dyn_lexer_next(&parser->lexer);
            subscript = dyn_expression(parser);
            dyn_take(parser, DYN_TOK_RBRACKET);
            if (dimension_count)
                element_size = parser->program->dimensions[
                    dimension_start
                ].stride;
            else if (operand < parser->program->count
                && parser->program->nodes[operand].kind == DYN_NODE_LOCAL)
                element_size = parser->program->locals[
                    parser->program->nodes[operand].value
                ].element_size;
            else if (operand < parser->program->count
                && parser->program->nodes[operand].kind == DYN_NODE_GLOBAL
                && parser->program->nodes[operand].value
                    < parser->program->global_count)
                element_size = parser->program->globals[
                    parser->program->nodes[operand].value
                ].element_size;
            else if (operand < parser->program->count
                && parser->program->nodes[operand].kind == DYN_NODE_MEMBER)
                element_size = parser->program->members[
                    parser->program->nodes[operand].value
                ].element_size;
            node = dyn_new_node(
                parser, DYN_NODE_SUBSCRIPT, element_size, operand, subscript
            );
            if (node != DYN_INVALID_NODE) {
                parser->program->nodes[node].extra = structure;
                if (dimension_count) {
                    parser->program->nodes[node].stride_node =
                        parser->program->dimensions[
                            dimension_start
                        ].stride_node;
                    parser->program->nodes[node].dimension_start =
                        dimension_start + 1u;
                    parser->program->nodes[node].dimension_count =
                        dimension_count - 1u;
                }
            }
            operand = node;
        } else {
            int arrow = parser->lexer.current.kind == DYN_TOK_ARROW;
            unsigned int structure = dyn_node_struct(parser, operand);
            unsigned int member;
            if ((arrow && !dyn_node_pointer(parser, operand))
                || (!arrow && dyn_node_pointer(parser, operand))) {
                parser->error = 1;
                return DYN_INVALID_NODE;
            }
            dyn_lexer_next(&parser->lexer);
            if (structure == DYN_INVALID_NODE
                || parser->lexer.current.kind != DYN_TOK_IDENTIFIER) {
                parser->error = 1;
                return DYN_INVALID_NODE;
            }
            member = dyn_find_member(
                parser, structure, parser->lexer.current.position,
                parser->lexer.current.length
            );
            if (member == DYN_INVALID_NODE) {
                parser->error = 1;
                return DYN_INVALID_NODE;
            }
            dyn_lexer_next(&parser->lexer);
            operand = dyn_new_node(
                parser, DYN_NODE_MEMBER, member, operand,
                arrow ? 1u : 0u
            );
            if (operand != DYN_INVALID_NODE) {
                parser->program->nodes[operand].dimension_start =
                    parser->program->members[member].dimension_start;
                parser->program->nodes[operand].dimension_count =
                    parser->program->members[member].dimension_count;
            }
        }
    }
    while (
        parser->lexer.current.kind == DYN_TOK_PLUS_PLUS
        || parser->lexer.current.kind == DYN_TOK_MINUS_MINUS
    ) {
        int increment = parser->lexer.current.kind == DYN_TOK_PLUS_PLUS;
        dyn_lexer_next(&parser->lexer);
        if (
            operand == DYN_INVALID_NODE
            || parser->program->nodes[operand].kind != DYN_NODE_LOCAL
        ) {
            parser->error = 1;
            return DYN_INVALID_NODE;
        }
        operand = dyn_new_node(
            parser, DYN_NODE_POST_INCREMENT, increment ? 1u : 0u,
            operand, DYN_INVALID_NODE
        );
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
        ) {
            const struct DynLocal *local = &parser->program->locals[
                parser->program->nodes[operand].value
            ];
            if (local->vla && !local->pointer)
                return local->total_size_node;
            size = local->size;
        } else if (
            operand < parser->program->count
            && (parser->program->nodes[operand].kind == DYN_NODE_DEREFERENCE
                || parser->program->nodes[operand].kind
                    == DYN_NODE_SUBSCRIPT)
        ) {
            if (!parser->program->nodes[operand].value
                && parser->program->nodes[operand].stride_node
                    != DYN_INVALID_NODE)
                return parser->program->nodes[operand].stride_node;
            size = parser->program->nodes[operand].value;
        } else size = 4u;
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
        && token != DYN_TOK_AMP
        && token != DYN_TOK_STAR
    ) return dyn_postfix(parser);
    dyn_lexer_next(&parser->lexer);
    operand = dyn_unary(parser);
    if (token == DYN_TOK_AMP)
        return dyn_new_node(
            parser, DYN_NODE_ADDRESS, 0, operand, DYN_INVALID_NODE
        );
    if (token == DYN_TOK_STAR) {
        unsigned int size = 4u;
        unsigned int structure = dyn_node_struct(parser, operand);
        unsigned int node;
        unsigned int dimension_start = 0u;
        unsigned int dimension_count = 0u;
        if (operand < parser->program->count) {
            dimension_start = parser->program->nodes[operand].dimension_start;
            dimension_count = parser->program->nodes[operand].dimension_count;
        }
        if (dimension_count)
            size = parser->program->dimensions[dimension_start].stride;
        if (operand < parser->program->count
            && !dimension_count
            && parser->program->nodes[operand].kind == DYN_NODE_LOCAL)
            size = parser->program->locals[
                parser->program->nodes[operand].value
            ].element_size;
        node = dyn_new_node(
            parser, DYN_NODE_DEREFERENCE, size,
            operand, DYN_INVALID_NODE
        );
        if (node != DYN_INVALID_NODE) {
            parser->program->nodes[node].extra = structure;
            if (dimension_count) {
                parser->program->nodes[node].stride_node =
                    parser->program->dimensions[dimension_start].stride_node;
                parser->program->nodes[node].dimension_start =
                    dimension_start + 1u;
                parser->program->nodes[node].dimension_count =
                    dimension_count - 1u;
            }
        }
        return node;
    }
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
            || (parser->program->nodes[left].kind != DYN_NODE_LOCAL
                && parser->program->nodes[left].kind != DYN_NODE_DEREFERENCE
                && parser->program->nodes[left].kind != DYN_NODE_SUBSCRIPT
                && parser->program->nodes[left].kind != DYN_NODE_GLOBAL
                && parser->program->nodes[left].kind != DYN_NODE_MEMBER)
        ) parser->error = 1;
        if (dyn_node_struct(parser, left) != DYN_INVALID_NODE
            && !dyn_node_pointer(parser, left))
            parser->error = 1;
        if (left < parser->program->count
            && parser->program->nodes[left].dimension_count)
            parser->error = 1;
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

static unsigned int dyn_sequence(
    struct DynParser *parser,
    unsigned int left,
    unsigned int right
) {
    if (left == DYN_INVALID_NODE) return right;
    return dyn_new_node(parser, DYN_NODE_SEQUENCE, 0, left, right);
}

static int dyn_declaration_start(const struct DynParser *parser) {
    int kind = parser->lexer.current.kind;
    return kind == DYN_TOK_INT || kind == DYN_TOK_UNSIGNED
        || kind == DYN_TOK_SIGNED || kind == DYN_TOK_CHAR_TYPE
        || kind == DYN_TOK_SHORT || kind == DYN_TOK_LONG
        || kind == DYN_TOK_CONST || kind == DYN_TOK_STRUCT
        || kind == DYN_TOK_ENUM
        || (kind == DYN_TOK_IDENTIFIER && dyn_find_alias(
            parser, parser->lexer.current.position,
            parser->lexer.current.length
        ) != DYN_INVALID_NODE);
}

static int dyn_finish_dimensions(
    struct DynParser *parser,
    struct DynParsedDimensions *result,
    unsigned int index
) {
    struct DynDimension *dimension;
    unsigned int product;
    if (!index) return 1;
    index -= 1u;
    dimension = &parser->program->dimensions[result->start + index];
    dimension->stride = result->total;
    dimension->stride_node = result->total_node;
    result->total_node = dyn_new_node(
        parser, DYN_NODE_MULTIPLY, 0u, result->total_node,
        dimension->count_node
    );
    if (!dimension->count || !result->total) result->total = 0u;
    else {
        product = result->total * dimension->count;
        if (dimension->count
            && product / dimension->count != result->total) {
            parser->error = 1;
            return 0;
        }
        result->total = product;
    }
    return dyn_finish_dimensions(parser, result, index);
}

static int dyn_parse_dimensions(
    struct DynParser *parser,
    unsigned int element_size,
    struct DynParsedDimensions *result
) {
    result->start = parser->program->dimension_count;
    result->count = 0u;
    result->variable = 0;
    while (parser->lexer.current.kind == DYN_TOK_LBRACKET) {
        unsigned int bound;
        unsigned int extent;
        if (parser->program->dimension_count
            >= parser->program->dimension_capacity) {
            parser->error = 1;
            return 0;
        }
        dyn_lexer_next(&parser->lexer);
        bound = dyn_expression(parser);
        dyn_take(parser, DYN_TOK_RBRACKET);
        if (!dyn_evaluate(parser->program, bound, &extent)) {
            extent = 0u;
            result->variable = 1;
        } else if (!extent) {
            parser->error = 1;
            return 0;
        }
        parser->program->dimensions[parser->program->dimension_count].count
            = extent;
        parser->program->dimensions[
            parser->program->dimension_count
        ].count_node = bound;
        parser->program->dimension_count += 1u;
        result->count += 1u;
    }
    result->total = element_size;
    result->total_node = dyn_new_node(
        parser, DYN_NODE_NUMBER, element_size,
        DYN_INVALID_NODE, DYN_INVALID_NODE
    );
    return dyn_finish_dimensions(parser, result, result->count);
}

static unsigned int dyn_declaration(
    struct DynParser *parser,
    int static_storage
) {
    unsigned int local;
    unsigned int initializer;
    unsigned int size = dyn_scalar_type(parser);
    unsigned int element_size = parser->type_element_size;
    unsigned int structure = parser->type_struct;
    unsigned int total_size_node = DYN_INVALID_NODE;
    int variable_array = 0;
    int pointer = parser->type_pointer;
    while (parser->lexer.current.kind == DYN_TOK_STAR) {
        element_size = size ? size : 4u;
        size = 4u;
        pointer = 1;
        dyn_lexer_next(&parser->lexer);
    }
    if (parser->lexer.current.kind != DYN_TOK_IDENTIFIER) {
        parser->error = 1;
        return DYN_INVALID_NODE;
    }
    local = dyn_add_local(parser, size, pointer);
    if (local != DYN_INVALID_NODE) {
        parser->program->locals[local].element_size = element_size;
        parser->program->locals[local].struct_id = structure;
    }
    dyn_lexer_next(&parser->lexer);
    if (static_storage) {
        struct DynGlobal *global;
        unsigned int value = 0u;
        unsigned int initializer_node = DYN_INVALID_NODE;
        if (structure != DYN_INVALID_NODE || pointer
            || parser->lexer.current.kind == DYN_TOK_LBRACKET
            || local == DYN_INVALID_NODE
            || parser->program->global_count
                >= parser->program->global_capacity) {
            parser->error = 1;
            return DYN_INVALID_NODE;
        }
        parser->frame_size -= 4u;
        if (parser->lexer.current.kind == DYN_TOK_ASSIGN) {
            dyn_lexer_next(&parser->lexer);
            initializer_node = dyn_assignment(parser);
            if (!dyn_evaluate(parser->program, initializer_node, &value)) {
                parser->error = 1;
                return DYN_INVALID_NODE;
            }
        }
        dyn_take(parser, DYN_TOK_SEMICOLON);
        global = &parser->program->globals[parser->program->global_count];
        parser->program->locals[local].static_storage = 1;
        parser->program->locals[local].static_global =
            parser->program->global_count;
        parser->program->global_count += 1u;
        global->name_position = parser->program->locals[local].position;
        global->name_length = parser->program->locals[local].length;
        global->size = size;
        global->element_size = element_size;
        global->count = 1u;
        global->initial_value = value;
        global->initializer_node = initializer_node;
        global->array = 0;
        global->pointer = 0;
        global->struct_id = DYN_INVALID_NODE;
        global->dimension_start = 0u;
        global->dimension_count = 0u;
        global->defined = 1;
        global->internal = 1;
        global->data = 0;
        global->data_length = 0u;
        return DYN_INVALID_NODE;
    }
    if (parser->lexer.current.kind == DYN_TOK_LBRACKET) {
        struct DynParsedDimensions dimensions;
        unsigned int previous_allocation =
            structure != DYN_INVALID_NODE && !pointer
                ? (size + 3u) & 0xfffffffcu : 4u;
        dyn_parse_dimensions(
            parser, size, &dimensions
        );
        total_size_node = dimensions.total_node;
        variable_array = dimensions.variable;
        parser->frame_size -= previous_allocation;
        parser->frame_size += variable_array
            ? 4u : (dimensions.total + 3u) & 0xfffffffcu;
        parser->program->locals[local].offset = parser->frame_size;
        parser->program->locals[local].element_size =
            parser->program->dimensions[dimensions.start].stride;
        parser->program->locals[local].count =
            parser->program->dimensions[dimensions.start].count;
        parser->program->locals[local].dimension_start = dimensions.start;
        parser->program->locals[local].dimension_count = dimensions.count;
        parser->program->locals[local].total_size_node = total_size_node;
        parser->program->locals[local].vla = variable_array;
        parser->program->locals[local].array = 1;
        parser->program->locals[local].size = element_size;
    }
    if (parser->lexer.current.kind == DYN_TOK_ASSIGN) {
        if ((structure != DYN_INVALID_NODE && !pointer) || variable_array)
            parser->error = 1;
        dyn_lexer_next(&parser->lexer);
        initializer = dyn_assignment(parser);
    } else if (variable_array) {
        unsigned int allocation;
        dyn_take(parser, DYN_TOK_SEMICOLON);
        allocation = dyn_new_node(
            parser, DYN_NODE_VLA_ALLOC, local, total_size_node,
            DYN_INVALID_NODE
        );
        return allocation;
    } else if (structure != DYN_INVALID_NODE && !pointer) {
        dyn_take(parser, DYN_TOK_SEMICOLON);
        return DYN_INVALID_NODE;
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

static unsigned int dyn_typedef_declaration(struct DynParser *parser) {
    unsigned int size;
    unsigned int element_size;
    unsigned int structure;
    unsigned int name_position;
    unsigned int name_length;
    unsigned int previous;
    int pointer;
    struct DynTypeAlias *alias;
    dyn_lexer_next(&parser->lexer);
    size = dyn_scalar_type(parser);
    element_size = parser->type_element_size;
    structure = parser->type_struct;
    pointer = parser->type_pointer;
    while (parser->lexer.current.kind == DYN_TOK_STAR) {
        element_size = size ? size : 4u;
        size = 4u;
        pointer = 1;
        dyn_lexer_next(&parser->lexer);
    }
    if (!size || parser->lexer.current.kind != DYN_TOK_IDENTIFIER
        || parser->program->alias_count >= parser->program->alias_capacity) {
        parser->error = 1;
        return DYN_INVALID_NODE;
    }
    name_position = parser->lexer.current.position;
    name_length = parser->lexer.current.length;
    previous = dyn_find_alias(parser, name_position, name_length);
    if (previous != DYN_INVALID_NODE
        && parser->program->aliases[previous].scope_depth
            == parser->scope_depth) {
        parser->error = 1;
        return DYN_INVALID_NODE;
    }
    dyn_lexer_next(&parser->lexer);
    dyn_take(parser, DYN_TOK_SEMICOLON);
    alias = &parser->program->aliases[parser->program->alias_count];
    parser->program->alias_count += 1u;
    alias->name_position = name_position;
    alias->name_length = name_length;
    alias->size = size;
    alias->element_size = element_size;
    alias->struct_id = structure;
    alias->pointer = pointer;
    alias->scope_depth = parser->scope_depth;
    alias->active = 1;
    return DYN_INVALID_NODE;
}

static void dyn_parse_enum(struct DynParser *parser);
static int dyn_enum_definition_start(struct DynParser *parser);

static unsigned int dyn_statement(struct DynParser *parser) {
    unsigned int node;
    if (parser->lexer.current.kind == DYN_TOK_LBRACE) {
        unsigned int sequence = DYN_INVALID_NODE;
        dyn_lexer_next(&parser->lexer);
        parser->scope_depth += 1u;
        while (
            parser->lexer.current.kind != DYN_TOK_RBRACE
            && parser->lexer.current.kind != DYN_TOK_EOF
            && !parser->error
        ) sequence = dyn_sequence(parser, sequence, dyn_statement(parser));
        dyn_take(parser, DYN_TOK_RBRACE);
        {
            unsigned int local = parser->local_base;
            while (local < parser->program->local_count) {
                if (parser->program->locals[local].scope_depth
                    == parser->scope_depth)
                    parser->program->locals[local].active = 0;
                local += 1u;
            }
        }
        {
            unsigned int alias = 0u;
            while (alias < parser->program->alias_count) {
                if (parser->program->aliases[alias].scope_depth
                    == parser->scope_depth)
                    parser->program->aliases[alias].active = 0;
                alias += 1u;
            }
        }
        {
            unsigned int tag = 0u;
            while (tag < parser->program->enum_count) {
                if (parser->program->enums[tag].scope_depth
                    == parser->scope_depth)
                    parser->program->enums[tag].active = 0;
                tag += 1u;
            }
        }
        {
            unsigned int constant = 0u;
            while (constant < parser->program->constant_count) {
                if (parser->program->constants[constant].scope_depth
                    == parser->scope_depth)
                    parser->program->constants[constant].active = 0;
                constant += 1u;
            }
        }
        parser->scope_depth -= 1u;
        return sequence;
    }
    if (dyn_enum_definition_start(parser)) {
        dyn_parse_enum(parser);
        return DYN_INVALID_NODE;
    }
    if (parser->lexer.current.kind == DYN_TOK_TYPEDEF)
        return dyn_typedef_declaration(parser);
    if (parser->lexer.current.kind == DYN_TOK_STATIC) {
        dyn_lexer_next(&parser->lexer);
        if (!dyn_declaration_start(parser)) {
            parser->error = 1;
            return DYN_INVALID_NODE;
        }
        return dyn_declaration(parser, 1);
    }
    if (dyn_declaration_start(parser))
        return dyn_declaration(parser, 0);
    if (parser->lexer.current.kind == DYN_TOK_RETURN) {
        dyn_lexer_next(&parser->lexer);
        if (parser->lexer.current.kind == DYN_TOK_SEMICOLON)
            node = dyn_new_node(
                parser, DYN_NODE_NUMBER, 0u,
                DYN_INVALID_NODE, DYN_INVALID_NODE
            );
        else node = dyn_expression(parser);
        dyn_take(parser, DYN_TOK_SEMICOLON);
        node = dyn_new_node(
            parser, DYN_NODE_RETURN, 0, node, DYN_INVALID_NODE
        );
        return node;
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
        if (dyn_declaration_start(parser))
            initializer = dyn_declaration(parser, 0);
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

static void dyn_parse_global(
    struct DynParser *parser,
    unsigned int name_position,
    unsigned int name_length,
    unsigned int size,
    unsigned int element_size,
    unsigned int structure,
    int pointer,
    int defined,
    int internal
) {
    struct DynGlobal *global;
    unsigned int count = 1u;
    unsigned int initial_value = 0u;
    unsigned int initializer_node = DYN_INVALID_NODE;
    char *data = 0;
    unsigned int data_length = 0;
    struct DynParsedDimensions dimensions;
    dimensions.start = 0u;
    dimensions.count = 0u;
    dimensions.total = size;
    dimensions.total_node = DYN_INVALID_NODE;
    dimensions.variable = 0;
    int array = 0;
    if (parser->program->global_count >= parser->program->global_capacity) {
        parser->error = 1;
        return;
    }
    if (parser->lexer.current.kind == DYN_TOK_LBRACKET) {
        array = 1;
        dyn_parse_dimensions(
            parser, size, &dimensions
        );
        if (dimensions.variable) parser->error = 1;
        count = parser->program->dimensions[dimensions.start].count;
        element_size = parser->program->dimensions[dimensions.start].stride;
    }
    if (parser->lexer.current.kind == DYN_TOK_ASSIGN) {
        unsigned int initializer;
        dyn_lexer_next(&parser->lexer);
        if (array && parser->lexer.current.kind == DYN_TOK_LBRACE) {
            unsigned int item = 0;
            data_length = dimensions.total;
            data = calloc(data_length, 1u);
            if (!data) parser->error = 1;
            dyn_lexer_next(&parser->lexer);
            while (parser->lexer.current.kind != DYN_TOK_RBRACE
                && !parser->error) {
                unsigned int value;
                unsigned int byte_index = 0;
                initializer = dyn_assignment(parser);
                if (item >= dimensions.total / size
                    || !dyn_evaluate(parser->program, initializer, &value)) {
                    parser->error = 1;
                    break;
                }
                while (byte_index < size) {
                    unsigned int shift = 8u * (size - byte_index - 1u);
                    data[item * size + byte_index]
                        = (char)(value >> shift);
                    byte_index += 1u;
                }
                item += 1u;
                if (parser->lexer.current.kind != DYN_TOK_COMMA) break;
                dyn_lexer_next(&parser->lexer);
                if (parser->lexer.current.kind == DYN_TOK_RBRACE) break;
            }
            dyn_take(parser, DYN_TOK_RBRACE);
        } else {
            initializer = dyn_assignment(parser);
            initializer_node = initializer;
            if (!dyn_evaluate(parser->program, initializer, &initial_value)) {
                int kind = initializer < parser->program->count
                    ? parser->program->nodes[initializer].kind : -1;
                if (kind != DYN_NODE_ADDRESS && kind != DYN_NODE_GLOBAL)
                    parser->error = 1;
            }
        }
        defined = 1;
    }
    dyn_take(parser, DYN_TOK_SEMICOLON);
    global = &parser->program->globals[parser->program->global_count];
    parser->program->global_count += 1u;
    global->name_position = name_position;
    global->name_length = name_length;
    global->size = size;
    global->element_size = element_size;
    global->count = count;
    global->initial_value = initial_value;
    global->initializer_node = initializer_node;
    global->array = array;
    global->pointer = pointer;
    global->struct_id = structure;
    global->dimension_start = dimensions.start;
    global->dimension_count = dimensions.count;
    global->defined = defined;
    global->internal = internal;
    global->data = data;
    global->data_length = data_length;
}

static void dyn_parse_enum(struct DynParser *parser) {
    unsigned int next_value = 0u;
    unsigned int tag_position = DYN_INVALID_NODE;
    unsigned int tag_length = 0u;
    dyn_take(parser, DYN_TOK_ENUM);
    if (parser->lexer.current.kind == DYN_TOK_IDENTIFIER) {
        tag_position = parser->lexer.current.position;
        tag_length = parser->lexer.current.length;
        dyn_lexer_next(&parser->lexer);
    }
    if (tag_position != DYN_INVALID_NODE) {
        struct DynEnumTag *tag;
        unsigned int existing = dyn_find_enum(
            parser, tag_position, tag_length
        );
        if ((existing != DYN_INVALID_NODE
                && parser->program->enums[existing].scope_depth
                    == parser->scope_depth)
            || parser->program->enum_count
                >= parser->program->enum_capacity) {
            parser->error = 1;
            return;
        }
        tag = &parser->program->enums[parser->program->enum_count];
        parser->program->enum_count += 1u;
        tag->name_position = tag_position;
        tag->name_length = tag_length;
        tag->scope_depth = parser->scope_depth;
        tag->active = 1;
    }
    if (!dyn_take(parser, DYN_TOK_LBRACE)) return;
    while (parser->lexer.current.kind != DYN_TOK_RBRACE && !parser->error) {
        struct DynConstant *constant;
        unsigned int existing;
        unsigned int position;
        unsigned int length;
        unsigned int value = next_value;
        if (parser->lexer.current.kind != DYN_TOK_IDENTIFIER) {
            parser->error = 1;
            break;
        }
        position = parser->lexer.current.position;
        length = parser->lexer.current.length;
        existing = dyn_find_constant(parser, position, length);
        if ((existing != DYN_INVALID_NODE
                && parser->program->constants[existing].scope_depth
                    == parser->scope_depth)
            || parser->program->constant_count
                >= parser->program->constant_capacity) {
            parser->error = 1;
            break;
        }
        dyn_lexer_next(&parser->lexer);
        if (parser->lexer.current.kind == DYN_TOK_ASSIGN) {
            unsigned int expression;
            dyn_lexer_next(&parser->lexer);
            expression = dyn_assignment(parser);
            if (!dyn_evaluate(parser->program, expression, &value)) {
                parser->error = 1;
                break;
            }
        }
        constant = &parser->program->constants[parser->program->constant_count];
        parser->program->constant_count += 1u;
        constant->name_position = position;
        constant->name_length = length;
        constant->value = value;
        constant->scope_depth = parser->scope_depth;
        constant->active = 1;
        next_value = value + 1u;
        if (parser->lexer.current.kind != DYN_TOK_COMMA) break;
        dyn_lexer_next(&parser->lexer);
        if (parser->lexer.current.kind == DYN_TOK_RBRACE) break;
    }
    dyn_take(parser, DYN_TOK_RBRACE);
    dyn_take(parser, DYN_TOK_SEMICOLON);
}

static int dyn_enum_definition_start(struct DynParser *parser) {
    struct DynLexer saved;
    int result = 0;
    if (parser->lexer.current.kind != DYN_TOK_ENUM) return 0;
    dyn_restore_lexer(&saved, &parser->lexer);
    dyn_lexer_next(&parser->lexer);
    if (parser->lexer.current.kind == DYN_TOK_IDENTIFIER)
        dyn_lexer_next(&parser->lexer);
    if (parser->lexer.current.kind == DYN_TOK_LBRACE) result = 1;
    dyn_restore_lexer(&parser->lexer, &saved);
    return result;
}

static int dyn_struct_definition_start(struct DynParser *parser) {
    struct DynLexer saved;
    int result = 0;
    if (parser->lexer.current.kind != DYN_TOK_STRUCT) return 0;
    dyn_restore_lexer(&saved, &parser->lexer);
    dyn_lexer_next(&parser->lexer);
    if (parser->lexer.current.kind == DYN_TOK_IDENTIFIER) {
        dyn_lexer_next(&parser->lexer);
        result = parser->lexer.current.kind == DYN_TOK_LBRACE;
    }
    dyn_restore_lexer(&parser->lexer, &saved);
    return result;
}

static int dyn_struct_forward_start(struct DynParser *parser) {
    struct DynLexer saved;
    int result = 0;
    if (parser->lexer.current.kind != DYN_TOK_STRUCT) return 0;
    dyn_restore_lexer(&saved, &parser->lexer);
    dyn_lexer_next(&parser->lexer);
    if (parser->lexer.current.kind == DYN_TOK_IDENTIFIER) {
        dyn_lexer_next(&parser->lexer);
        result = parser->lexer.current.kind == DYN_TOK_SEMICOLON;
    }
    dyn_restore_lexer(&parser->lexer, &saved);
    return result;
}

static void dyn_parse_struct_forward(struct DynParser *parser) {
    unsigned int position;
    unsigned int length;
    unsigned int found;
    dyn_take(parser, DYN_TOK_STRUCT);
    if (parser->lexer.current.kind != DYN_TOK_IDENTIFIER) {
        parser->error = 1;
        return;
    }
    position = parser->lexer.current.position;
    length = parser->lexer.current.length;
    found = dyn_find_struct(parser, position, length);
    if (found == DYN_INVALID_NODE) {
        struct DynStruct *structure;
        if (parser->program->struct_count >= parser->program->struct_capacity) {
            parser->error = 1;
            return;
        }
        structure = &parser->program->structs[parser->program->struct_count];
        parser->program->struct_count += 1u;
        structure->name_position = position;
        structure->name_length = length;
        structure->size = 0u;
        structure->alignment = 1u;
        structure->member_start = parser->program->member_count;
        structure->member_count = 0u;
        structure->defined = 0;
    }
    dyn_lexer_next(&parser->lexer);
    dyn_take(parser, DYN_TOK_SEMICOLON);
}

static void dyn_parse_struct_definition(struct DynParser *parser) {
    struct DynStruct *structure;
    unsigned int structure_index;
    unsigned int tag_position;
    unsigned int tag_length;
    unsigned int offset = 0u;
    unsigned int alignment = 1u;
    dyn_take(parser, DYN_TOK_STRUCT);
    if (parser->lexer.current.kind != DYN_TOK_IDENTIFIER) {
        parser->error = 1;
        return;
    }
    tag_position = parser->lexer.current.position;
    tag_length = parser->lexer.current.length;
    structure_index = dyn_find_struct(parser, tag_position, tag_length);
    if (structure_index != DYN_INVALID_NODE
        && parser->program->structs[structure_index].defined) {
        parser->error = 1;
        return;
    }
    if (structure_index == DYN_INVALID_NODE
        && parser->program->struct_count >= parser->program->struct_capacity) {
        parser->error = 1;
        return;
    }
    dyn_lexer_next(&parser->lexer);
    dyn_take(parser, DYN_TOK_LBRACE);
    if (structure_index == DYN_INVALID_NODE) {
        structure_index = parser->program->struct_count;
        parser->program->struct_count += 1u;
    }
    structure = &parser->program->structs[structure_index];
    structure->name_position = tag_position;
    structure->name_length = tag_length;
    structure->size = 0u;
    structure->alignment = 1u;
    structure->member_start = parser->program->member_count;
    structure->member_count = 0u;
    structure->defined = 0;
    while (parser->lexer.current.kind != DYN_TOK_RBRACE && !parser->error) {
        struct DynMember *member;
        unsigned int member_size = dyn_scalar_type(parser);
        unsigned int member_structure = parser->type_struct;
        unsigned int element_size = parser->type_element_size;
        struct DynParsedDimensions dimensions;
        unsigned int member_alignment;
        int pointer = parser->type_pointer;
        int array = 0;
        unsigned int check;
        dimensions.start = 0u;
        dimensions.count = 0u;
        dimensions.total = member_size;
        dimensions.total_node = DYN_INVALID_NODE;
        dimensions.variable = 0;
        while (parser->lexer.current.kind == DYN_TOK_STAR) {
            element_size = member_size ? member_size : 4u;
            member_size = 4u;
            pointer = 1;
            dyn_lexer_next(&parser->lexer);
        }
        if (!member_size) { parser->error = 1; break; }
        if (parser->lexer.current.kind != DYN_TOK_IDENTIFIER
            || parser->program->member_count
                >= parser->program->member_capacity) {
            parser->error = 1;
            break;
        }
        check = structure->member_start;
        while (check < parser->program->member_count) {
            const struct DynMember *old = &parser->program->members[check];
            if (dyn_same_name(
                parser, old->name_position, old->name_length,
                parser->lexer.current.position, parser->lexer.current.length
            )) parser->error = 1;
            check += 1u;
        }
        member = &parser->program->members[parser->program->member_count];
        member->name_position = parser->lexer.current.position;
        member->name_length = parser->lexer.current.length;
        dyn_lexer_next(&parser->lexer);
        if (parser->lexer.current.kind == DYN_TOK_LBRACKET) {
            array = 1;
            dyn_parse_dimensions(
                parser, member_size, &dimensions
            );
            if (dimensions.variable) parser->error = 1;
        }
        dyn_take(parser, DYN_TOK_SEMICOLON);
        member_alignment = pointer ? 4u
            : member_structure != DYN_INVALID_NODE
                ? parser->program->structs[member_structure].alignment
                : member_size;
        if (member_alignment > 4u) member_alignment = 4u;
        while (offset & (member_alignment - 1u)) offset += 1u;
        member->offset = offset;
        member->size = member_size;
        member->element_size = array
            ? parser->program->dimensions[dimensions.start].stride
            : element_size;
        member->struct_id = member_structure;
        member->dimension_start = dimensions.start;
        member->dimension_count = dimensions.count;
        member->pointer = pointer;
        member->array = array;
        offset += array ? dimensions.total : member_size;
        if (member_alignment > alignment) alignment = member_alignment;
        parser->program->member_count += 1u;
        structure->member_count += 1u;
    }
    dyn_take(parser, DYN_TOK_RBRACE);
    dyn_take(parser, DYN_TOK_SEMICOLON);
    while (offset & (alignment - 1u)) offset += 1u;
    structure->size = offset;
    structure->alignment = alignment;
    structure->defined = 1;
    {
        unsigned int member_index = structure->member_start;
        while (member_index < structure->member_start + structure->member_count) {
            struct DynMember *member = &parser->program->members[member_index];
            if (member->pointer && member->struct_id == structure_index)
                member->element_size = offset;
            member_index += 1u;
        }
    }
    {
        unsigned int alias_index = 0u;
        while (alias_index < parser->program->alias_count) {
            struct DynTypeAlias *alias = &parser->program->aliases[alias_index];
            if (alias->pointer && alias->struct_id == structure_index)
                alias->element_size = offset;
            alias_index += 1u;
        }
    }
    {
        unsigned int global_index = 0u;
        while (global_index < parser->program->global_count) {
            struct DynGlobal *global = &parser->program->globals[global_index];
            if (global->pointer && global->struct_id == structure_index)
                global->element_size = offset;
            global_index += 1u;
        }
    }
}

int dyn_parse(
    const char *source,
    unsigned int length,
    struct DynAstProgram *program
) {
    struct DynParser parser;
    unsigned int index;
    parser.program = program;
    parser.error = 0;
    parser.local_base = 0;
    parser.frame_size = 0;
    parser.scope_depth = 0;
    parser.type_struct = DYN_INVALID_NODE;
    parser.type_element_size = 0u;
    parser.type_pointer = 0;
    program->count = 0;
    program->local_count = 0;
    program->function_count = 0;
    program->global_count = 0;
    program->constant_count = 0;
    program->alias_count = 0;
    program->enum_count = 0;
    program->struct_count = 0;
    program->member_count = 0;
    program->dimension_count = 0;
    program->main_function = DYN_INVALID_NODE;
    program->source = source;
    program->expression = DYN_INVALID_NODE;
    dyn_lexer_init(&parser.lexer, source, length);
    while (parser.lexer.current.kind != DYN_TOK_EOF && !parser.error) {
        unsigned int name_position;
        unsigned int name_length;
        unsigned int local_base;
        unsigned int parameter_count = 0;
        unsigned int function_index;
        unsigned int type_size;
        unsigned int element_size;
        int pointer = 0;
        int external = 0;
        int internal = 0;
        int type_alias = 0;
        struct DynFunction *function;
        if (dyn_enum_definition_start(&parser)) {
            dyn_parse_enum(&parser);
            continue;
        }
        if (dyn_struct_definition_start(&parser)) {
            dyn_parse_struct_definition(&parser);
            continue;
        }
        if (dyn_struct_forward_start(&parser)) {
            dyn_parse_struct_forward(&parser);
            continue;
        }
        if (parser.lexer.current.kind == DYN_TOK_TYPEDEF) {
            type_alias = 1;
            dyn_lexer_next(&parser.lexer);
        }
        while (parser.lexer.current.kind == DYN_TOK_STATIC
            || parser.lexer.current.kind == DYN_TOK_EXTERN) {
            if (parser.lexer.current.kind == DYN_TOK_STATIC) internal = 1;
            else external = 1;
            dyn_lexer_next(&parser.lexer);
        }
        type_size = dyn_scalar_type(&parser);
        element_size = parser.type_element_size;
        {
            unsigned int structure = parser.type_struct;
        pointer = parser.type_pointer;
        while (parser.lexer.current.kind == DYN_TOK_STAR) {
            element_size = type_size ? type_size : 4u;
            type_size = 4u;
            pointer = 1;
            dyn_lexer_next(&parser.lexer);
        }
        if (!type_size) { parser.error = 1; break; }
        if (
            parser.lexer.current.kind != DYN_TOK_IDENTIFIER
            && parser.lexer.current.kind != DYN_TOK_MAIN
        ) { parser.error = 1; break; }
        name_position = parser.lexer.current.position;
        name_length = parser.lexer.current.length;
        dyn_lexer_next(&parser.lexer);
        if (type_alias) {
            struct DynTypeAlias *alias;
            if (external || internal
                || program->alias_count >= program->alias_capacity
                || dyn_find_alias(
                    &parser, name_position, name_length
                ) != DYN_INVALID_NODE) {
                parser.error = 1;
                break;
            }
            dyn_take(&parser, DYN_TOK_SEMICOLON);
            alias = &program->aliases[program->alias_count];
            program->alias_count += 1u;
            alias->name_position = name_position;
            alias->name_length = name_length;
            alias->size = type_size;
            alias->element_size = element_size;
            alias->struct_id = structure;
            alias->pointer = pointer;
            alias->scope_depth = 0u;
            alias->active = 1;
            continue;
        }
        if (parser.lexer.current.kind != DYN_TOK_LPAREN) {
            parser.local_base = program->local_count;
            parser.scope_depth = 0;
            dyn_parse_global(
                &parser, name_position, name_length, type_size,
                element_size, structure, pointer, !external, internal
            );
            continue;
        }
        if (structure != DYN_INVALID_NODE && !pointer) {
            parser.error = 1;
            break;
        }
        dyn_take(&parser, DYN_TOK_LPAREN);
        local_base = program->local_count;
        parser.local_base = local_base;
        parser.frame_size = 0;
        parser.scope_depth = 0;
        if (parser.lexer.current.kind == DYN_TOK_VOID) {
            struct DynLexer saved;
            dyn_restore_lexer(&saved, &parser.lexer);
            dyn_lexer_next(&parser.lexer);
            if (parser.lexer.current.kind != DYN_TOK_RPAREN)
                dyn_restore_lexer(&parser.lexer, &saved);
        }
        while (
            parser.lexer.current.kind != DYN_TOK_RPAREN && !parser.error
        ) {
            unsigned int parameter_size = dyn_scalar_type(&parser);
            unsigned int parameter_structure = parser.type_struct;
            unsigned int parameter_element_size = parser.type_element_size;
            int parameter_pointer = parser.type_pointer;
            while (parser.lexer.current.kind == DYN_TOK_STAR) {
                parameter_element_size = parameter_size ? parameter_size : 4u;
                parameter_size = 4u;
                parameter_pointer = 1;
                dyn_lexer_next(&parser.lexer);
            }
            if (!parameter_size) { parser.error = 1; break; }
            if (parameter_structure != DYN_INVALID_NODE
                && !parameter_pointer) {
                parser.error = 1;
                break;
            }
            if (parser.lexer.current.kind != DYN_TOK_IDENTIFIER) {
                parser.error = 1;
                break;
            }
            {
                unsigned int parameter = dyn_add_local(
                    &parser, parameter_size, parameter_pointer
                );
                if (parameter != DYN_INVALID_NODE) {
                    program->locals[parameter].element_size =
                        parameter_element_size;
                    program->locals[parameter].struct_id = parameter_structure;
                }
                parameter_count += 1u;
                dyn_lexer_next(&parser.lexer);
                if (parser.lexer.current.kind == DYN_TOK_LBRACKET) {
                    struct DynParsedDimensions dimensions;
                    dyn_parse_dimensions(
                        &parser, parameter_size, &dimensions
                    );
                    if (parameter != DYN_INVALID_NODE) {
                        program->locals[parameter].size = 4u;
                        program->locals[parameter].pointer = 1;
                        program->locals[parameter].element_size =
                            program->dimensions[dimensions.start].stride;
                        program->locals[parameter].dimension_start =
                            dimensions.start;
                        program->locals[parameter].dimension_count =
                            dimensions.count;
                    }
                }
            }
            if (parser.lexer.current.kind != DYN_TOK_COMMA) break;
            dyn_lexer_next(&parser.lexer);
        }
        dyn_take(&parser, DYN_TOK_RPAREN);
        if (program->function_count >= program->function_capacity) {
            parser.error = 1;
            break;
        }
        function_index = program->function_count;
        program->function_count += 1u;
        function = &program->functions[function_index];
        function->name_position = name_position;
        function->name_length = name_length;
        function->local_base = local_base;
        function->parameter_count = parameter_count;
        function->body = DYN_INVALID_NODE;
        function->defined = 0;
        if (parser.lexer.current.kind == DYN_TOK_SEMICOLON) {
            dyn_lexer_next(&parser.lexer);
            program->local_count = local_base;
            function->local_count = 0;
        } else {
            function->body = dyn_statement(&parser);
            function->defined = 1;
            function->local_count = program->local_count - local_base;
            function->frame_size = parser.frame_size;
            if (dyn_same_name(
                &parser, name_position, name_length,
                name_position, 4u
            ) && name_length == 4u
                && source[name_position] == 'm'
                && source[name_position + 1u] == 'a'
                && source[name_position + 2u] == 'i'
                && source[name_position + 3u] == 'n')
                program->main_function = function_index;
        }
        }
    }
    if (program->main_function == DYN_INVALID_NODE) parser.error = 1;
    index = 0;
    while (index < program->count && !parser.error) {
        struct DynNode *node = &program->nodes[index];
        if (node->kind == DYN_NODE_CALL) {
            unsigned int candidate = program->function_count;
            unsigned int found = DYN_INVALID_NODE;
            while (candidate) {
                struct DynFunction *function;
                candidate -= 1u;
                function = &program->functions[candidate];
                if (function->defined && dyn_same_name(
                    &parser,
                    node->value,
                    node->extra,
                    function->name_position,
                    function->name_length
                )) { found = candidate; break; }
            }
            if (found == DYN_INVALID_NODE) parser.error = 1;
            else node->value = found;
        } else if (node->kind == DYN_NODE_GLOBAL
            && node->right == DYN_INVALID_NODE) {
            unsigned int candidate = program->global_count;
            unsigned int found = DYN_INVALID_NODE;
            while (candidate) {
                struct DynGlobal *global;
                candidate -= 1u;
                global = &program->globals[candidate];
                if (global->defined && dyn_same_name(
                    &parser, node->value, node->extra,
                    global->name_position, global->name_length
                )) { found = candidate; break; }
            }
            if (found == DYN_INVALID_NODE) parser.error = 1;
            else {
                node->value = found;
                node->dimension_start = program->globals[found].dimension_start;
                node->dimension_count = program->globals[found].dimension_count;
            }
        } else if (node->kind == DYN_NODE_SUBSCRIPT
            && node->left < program->count
            && program->nodes[node->left].kind == DYN_NODE_GLOBAL) {
            unsigned int global_index = program->nodes[node->left].value;
            if (global_index < program->global_count) {
                const struct DynNode *left = &program->nodes[node->left];
                node->extra = program->globals[global_index].struct_id;
                if (left->dimension_count) {
                    node->value = program->dimensions[
                        left->dimension_start
                    ].stride;
                    node->stride_node = program->dimensions[
                        left->dimension_start
                    ].stride_node;
                    node->dimension_start = left->dimension_start + 1u;
                    node->dimension_count = left->dimension_count - 1u;
                } else node->value =
                    program->globals[global_index].element_size;
            }
        }
        index += 1u;
    }
    program->error_position = parser.error
        ? parser.lexer.current.position : DYN_INVALID_NODE;
    if (!parser.error)
        program->expression = program->functions[program->main_function].body;
    return parser.error ? 0 : 1;
}
