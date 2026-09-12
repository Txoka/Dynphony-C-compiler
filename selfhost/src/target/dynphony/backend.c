#include <stdlib.h>
#include "dynphony/target.h"

#define DYN_INVALID_NODE 0xffffffffu

struct DynEmitter {
    const struct DynAstProgram *program;
    char *output;
    unsigned int capacity;
    unsigned int position;
    unsigned int load_address;
    unsigned int *returns;
    unsigned int return_count;
    unsigned int return_capacity;
    unsigned int *breaks;
    unsigned int *continues;
    unsigned int break_count;
    unsigned int continue_count;
    unsigned int loop_depth;
    unsigned int current_local_base;
    unsigned int *function_offsets;
    unsigned int *call_fixups;
    unsigned int *call_targets;
    unsigned int call_count;
    unsigned int *global_offsets;
    unsigned int *global_fixups;
    unsigned int *global_targets;
    unsigned int global_fixup_count;
    int error;
};

static void dyn_byte(struct DynEmitter *e, unsigned int value) {
    if (e->position >= e->capacity) { e->error = 1; return; }
    e->output[e->position] = (char)(value & 255u);
    e->position += 1u;
}

static void dyn_u16(struct DynEmitter *e, unsigned int value) {
    dyn_byte(e, value >> 8); dyn_byte(e, value);
}

static void dyn_alu(struct DynEmitter *e, unsigned int op, unsigned int d,
                    unsigned int a, unsigned int b) {
    dyn_byte(e, op); dyn_byte(e, (d << 4) | a); dyn_byte(e, b);
}

static void dyn_alu_immediate(struct DynEmitter *e, unsigned int op,
                              unsigned int d, unsigned int a,
                              unsigned int value) {
    dyn_byte(e, op + 0x10u); dyn_byte(e, (d << 4) | a); dyn_u16(e, value);
}

static void dyn_move(struct DynEmitter *e, unsigned int d, unsigned int s) {
    dyn_alu(e, 0x21u, d, 0u, s);
}

static void dyn_push(struct DynEmitter *e, unsigned int reg) {
    dyn_alu_immediate(e, 0x25u, 14u, 14u, 4u);
    dyn_byte(e, 0x66u); dyn_byte(e, reg); dyn_byte(e, 14u);
}

static void dyn_pop(struct DynEmitter *e, unsigned int reg) {
    dyn_byte(e, 0x62u); dyn_byte(e, reg << 4); dyn_byte(e, 14u);
    dyn_alu_immediate(e, 0x24u, 14u, 14u, 4u);
}

static void dyn_constant_at(struct DynEmitter *e, unsigned int position,
                            unsigned int reg, unsigned int value) {
    unsigned int saved = e->position;
    e->position = position;
    dyn_byte(e, 0x31u); dyn_byte(e, reg << 4); dyn_u16(e, value >> 16);
    dyn_byte(e, 0x37u); dyn_byte(e, (reg << 4) | reg); dyn_u16(e, 16u);
    dyn_byte(e, 0x31u); dyn_byte(e, (reg << 4) | reg); dyn_u16(e, value);
    e->position = saved;
}

static void dyn_write_u32_at(struct DynEmitter *e, unsigned int position,
                             unsigned int value) {
    if (position + 4u > e->capacity) { e->error = 1; return; }
    e->output[position] = (char)(value >> 24);
    e->output[position + 1u] = (char)(value >> 16);
    e->output[position + 2u] = (char)(value >> 8);
    e->output[position + 3u] = (char)value;
}

static void dyn_constant(struct DynEmitter *e, unsigned int reg,
                         unsigned int value) {
    unsigned int position = e->position;
    unsigned int end = position + 12u;
    if (end < position || end > e->capacity) { e->error = 1; return; }
    e->position = end;
    dyn_constant_at(e, position, reg, value);
}

static unsigned int dyn_branch(struct DynEmitter *e, unsigned int op) {
    unsigned int fixup = e->position;
    dyn_constant(e, 7u, 0u);
    dyn_byte(e, op); dyn_byte(e, 0x0fu); dyn_byte(e, 0x07u);
    return fixup;
}

static void dyn_patch(struct DynEmitter *e, unsigned int fixup,
                      unsigned int target) {
    dyn_constant_at(e, fixup, 7u, e->load_address + target);
}

static void dyn_compare_zero(struct DynEmitter *e, unsigned int reg) {
    dyn_byte(e, 0x3au); dyn_byte(e, reg); dyn_u16(e, 0u);
}

static void dyn_local_address(struct DynEmitter *e, unsigned int local,
                              unsigned int destination) {
    unsigned int offset;
    if (local < e->current_local_base) { e->error = 1; return; }
    offset = e->program->locals[local].offset;
    if (offset <= 65535u)
        dyn_alu_immediate(e, 0x25u, destination, 12u, offset);
    else {
        dyn_constant(e, destination, offset);
        dyn_alu(e, 0x25u, destination, 12u, destination);
    }
}

static void dyn_global_address(struct DynEmitter *e, unsigned int global,
                               unsigned int destination) {
    unsigned int fixup;
    if (global >= e->program->global_count
        || e->global_fixup_count >= e->return_capacity) {
        e->error = 1; return;
    }
    fixup = e->position;
    dyn_constant(e, 7u, 0u);
    e->global_fixups[e->global_fixup_count] = fixup;
    e->global_targets[e->global_fixup_count] = global;
    e->global_fixup_count += 1u;
    if (destination != 7u) dyn_move(e, destination, 7u);
}

static void dyn_call(struct DynEmitter *e, unsigned int function) {
    unsigned int fixup;
    if (e->call_count >= e->return_capacity) { e->error = 1; return; }
    fixup = e->position; dyn_constant(e, 7u, 0u);
    e->call_fixups[e->call_count] = fixup;
    e->call_targets[e->call_count] = function;
    e->call_count += 1u;
    dyn_byte(e, 0x07u); dyn_byte(e, 0xf0u);
    dyn_alu_immediate(e, 0x24u, 15u, 15u, 16u);
    dyn_push(e, 15u);
    dyn_byte(e, 0x48u); dyn_byte(e, 0x0fu); dyn_byte(e, 7u);
}

static void dyn_return_instruction(struct DynEmitter *e) {
    dyn_pop(e, 15u);
    dyn_byte(e, 0x48u); dyn_byte(e, 0x0fu); dyn_byte(e, 15u);
}

static unsigned int dyn_memory_operation(unsigned int size, int store) {
    if (size == 1u) return store ? 0x64u : 0x60u;
    if (size == 2u) return store ? 0x65u : 0x61u;
    return store ? 0x66u : 0x62u;
}

static void dyn_boolean(struct DynEmitter *e, unsigned int reg,
                        unsigned int jump_if_true) {
    unsigned int yes;
    unsigned int done;
    dyn_constant(e, reg, 0u);
    yes = dyn_branch(e, jump_if_true);
    done = dyn_branch(e, 0x48u);
    dyn_patch(e, yes, e->position);
    dyn_constant(e, reg, 1u);
    dyn_patch(e, done, e->position);
}

static void dyn_multiply(struct DynEmitter *e, unsigned int reg) {
    unsigned int loop;
    unsigned int skip;
    unsigned int done;
    if (reg + 3u >= 7u) { e->error = 1; return; }
    dyn_constant(e, reg + 2u, 0u); loop = e->position;
    dyn_compare_zero(e, reg + 1u); done = dyn_branch(e, 0x41u);
    dyn_alu_immediate(e, 0x22u, reg + 3u, reg + 1u, 1u);
    dyn_compare_zero(e, reg + 3u); skip = dyn_branch(e, 0x41u);
    dyn_alu(e, 0x24u, reg + 2u, reg + 2u, reg);
    dyn_patch(e, skip, e->position);
    dyn_alu_immediate(e, 0x27u, reg, reg, 1u);
    dyn_alu_immediate(e, 0x28u, reg + 1u, reg + 1u, 1u);
    skip = dyn_branch(e, 0x48u); dyn_patch(e, skip, loop);
    dyn_patch(e, done, e->position); dyn_move(e, reg, reg + 2u);
}

static void dyn_divide(struct DynEmitter *e, unsigned int reg,
                       int remainder_result) {
    unsigned int zero;
    unsigned int loop;
    unsigned int skip;
    unsigned int back;
    unsigned int done;
    if (reg + 5u >= 7u) { e->error = 1; return; }
    dyn_compare_zero(e, reg + 1u); zero = dyn_branch(e, 0x41u);
    dyn_constant(e, reg + 2u, 0u);
    dyn_constant(e, reg + 3u, 0u);
    dyn_constant(e, reg + 4u, 32u);
    loop = e->position;
    dyn_alu_immediate(e, 0x27u, reg + 2u, reg + 2u, 1u);
    dyn_alu_immediate(e, 0x27u, reg + 3u, reg + 3u, 1u);
    dyn_alu_immediate(e, 0x28u, reg + 5u, reg, 31u);
    dyn_alu(e, 0x21u, reg + 3u, reg + 3u, reg + 5u);
    dyn_alu_immediate(e, 0x27u, reg, reg, 1u);
    dyn_byte(e, 0x2au); dyn_byte(e, reg + 3u); dyn_byte(e, reg + 1u);
    skip = dyn_branch(e, 0x42u);
    dyn_alu(e, 0x25u, reg + 3u, reg + 3u, reg + 1u);
    dyn_alu_immediate(e, 0x21u, reg + 2u, reg + 2u, 1u);
    dyn_patch(e, skip, e->position);
    dyn_alu_immediate(e, 0x25u, reg + 4u, reg + 4u, 1u);
    dyn_compare_zero(e, reg + 4u); back = dyn_branch(e, 0x49u);
    dyn_patch(e, back, loop);
    dyn_move(e, reg, remainder_result ? reg + 3u : reg + 2u);
    done = dyn_branch(e, 0x48u);
    dyn_patch(e, zero, e->position); dyn_constant(e, reg, 0u);
    dyn_patch(e, done, e->position);
}

static void dyn_expression(struct DynEmitter *e,
                           const struct DynAstProgram *program,
                           unsigned int index, unsigned int reg);

static void dyn_lvalue_address(struct DynEmitter *e,
                               const struct DynAstProgram *program,
                               unsigned int index, unsigned int reg) {
    const struct DynNode *node;
    if (index >= program->count || reg >= 6u) { e->error = 1; return; }
    node = &program->nodes[index];
    if (node->kind == DYN_NODE_LOCAL) {
        dyn_local_address(e, node->value, reg);
    } else if (node->kind == DYN_NODE_GLOBAL) {
        dyn_global_address(e, node->value, reg);
    } else if (node->kind == DYN_NODE_DEREFERENCE) {
        dyn_expression(e, program, node->left, reg);
    } else if (node->kind == DYN_NODE_SUBSCRIPT) {
        dyn_expression(e, program, node->left, reg);
        dyn_push(e, reg);
        dyn_expression(e, program, node->right, reg + 1u);
        dyn_pop(e, reg);
        if (node->value == 2u)
            dyn_alu_immediate(e, 0x27u, reg + 1u, reg + 1u, 1u);
        else if (node->value == 4u)
            dyn_alu_immediate(e, 0x27u, reg + 1u, reg + 1u, 2u);
        else if (node->value != 1u) {
            dyn_constant(e, reg + 2u, node->value);
            dyn_multiply(e, reg + 1u);
        }
        dyn_alu(e, 0x24u, reg, reg, reg + 1u);
    } else if (node->kind == DYN_NODE_MEMBER) {
        const struct DynMember *member;
        if (node->value >= program->member_count) { e->error = 1; return; }
        member = &program->members[node->value];
        if (node->right) dyn_expression(e, program, node->left, reg);
        else dyn_lvalue_address(e, program, node->left, reg);
        if (member->offset <= 65535u)
            dyn_alu_immediate(e, 0x24u, reg, reg, member->offset);
        else {
            dyn_constant(e, reg + 1u, member->offset);
            dyn_alu(e, 0x24u, reg, reg, reg + 1u);
        }
    } else e->error = 1;
}

static unsigned int dyn_lvalue_size(const struct DynAstProgram *program,
                                    unsigned int index) {
    const struct DynNode *node = &program->nodes[index];
    if (node->kind == DYN_NODE_LOCAL)
        return program->locals[node->value].size;
    if (node->kind == DYN_NODE_GLOBAL)
        return program->globals[node->value].size;
    if (node->kind == DYN_NODE_MEMBER)
        return program->members[node->value].size;
    if (node->kind == DYN_NODE_DEREFERENCE || node->kind == DYN_NODE_SUBSCRIPT)
        return node->value;
    return 4u;
}

static unsigned int dyn_pointer_element(const struct DynAstProgram *program,
                                        unsigned int index) {
    const struct DynNode *node;
    if (index >= program->count) return 0u;
    node = &program->nodes[index];
    if (node->kind == DYN_NODE_LOCAL) {
        const struct DynLocal *local = &program->locals[node->value];
        return local->pointer || local->array ? local->element_size : 0u;
    }
    if (node->kind == DYN_NODE_GLOBAL) {
        const struct DynGlobal *global = &program->globals[node->value];
        return global->pointer || global->array ? global->element_size : 0u;
    }
    if (node->kind == DYN_NODE_MEMBER) {
        const struct DynMember *member = &program->members[node->value];
        return member->pointer || member->array ? member->element_size : 0u;
    }
    if (node->kind == DYN_NODE_ADDRESS)
        return dyn_lvalue_size(program, node->left);
    if (node->kind == DYN_NODE_ADD) {
        unsigned int left = dyn_pointer_element(program, node->left);
        return left ? left : dyn_pointer_element(program, node->right);
    }
    if (node->kind == DYN_NODE_SUBTRACT)
        return dyn_pointer_element(program, node->left);
    return 0u;
}

static void dyn_expression(struct DynEmitter *e,
                           const struct DynAstProgram *program,
                           unsigned int index, unsigned int reg) {
    const struct DynNode *node;
    unsigned int operation = 0;
    unsigned int fixup;
    unsigned int done;
    if (index >= program->count || reg >= 7u) { e->error = 1; return; }
    node = &program->nodes[index];
    if (node->kind == DYN_NODE_NUMBER) { dyn_constant(e, reg, node->value); return; }
    if (node->kind == DYN_NODE_LOCAL) {
        if (node->value >= program->local_count) { e->error = 1; return; }
        dyn_local_address(e, node->value, 7u);
        if (program->locals[node->value].array) {
            dyn_move(e, reg, 7u); return;
        }
        dyn_byte(e, dyn_memory_operation(program->locals[node->value].size, 0));
        dyn_byte(e, reg << 4); dyn_byte(e, 7u); return;
    }
    if (node->kind == DYN_NODE_GLOBAL) {
        if (node->value >= program->global_count) { e->error = 1; return; }
        dyn_global_address(e, node->value, 7u);
        if (program->globals[node->value].array) {
            dyn_move(e, reg, 7u); return;
        }
        dyn_byte(e, dyn_memory_operation(program->globals[node->value].size, 0));
        dyn_byte(e, reg << 4); dyn_byte(e, 7u); return;
    }
    if (node->kind == DYN_NODE_MEMBER) {
        const struct DynMember *member;
        if (node->value >= program->member_count) { e->error = 1; return; }
        member = &program->members[node->value];
        dyn_lvalue_address(e, program, index, reg);
        if (member->array
            || (member->struct_id != DYN_INVALID_NODE && !member->pointer))
            return;
        dyn_byte(e, dyn_memory_operation(member->size, 0));
        dyn_byte(e, reg << 4); dyn_byte(e, reg); return;
    }
    if (node->kind == DYN_NODE_CALL_INPUT) {
        dyn_byte(e, 0x01u); dyn_byte(e, reg << 4); return;
    }
    if (node->kind == DYN_NODE_CALL_OUTPUT) {
        dyn_expression(e, program, node->left, reg);
        dyn_byte(e, 0x02u); dyn_byte(e, 0u); dyn_byte(e, reg);
        dyn_constant(e, reg, 0u); return;
    }
    if (node->kind == DYN_NODE_CALL_INTRINSIC) {
        if (node->value == 1u) {
            dyn_byte(e, 0x03u); dyn_byte(e, reg << 4);
        } else if (node->value == 2u || node->value == 3u) {
            dyn_byte(e, node->value == 2u ? 0x05u : 0x06u);
            dyn_byte(e, reg << 4);
        } else if (node->value == 5u) {
            dyn_expression(e, program, node->left, reg);
            dyn_byte(e, 0x63u); dyn_byte(e, reg << 4); dyn_byte(e, reg);
        } else if (node->value == 4u || node->value == 6u) {
            dyn_expression(e, program, node->left, reg);
            dyn_expression(e, program, node->right, reg + 1u);
            if (node->value == 4u) {
                dyn_byte(e, 0x04u); dyn_byte(e, reg); dyn_byte(e, reg + 1u);
            } else {
                dyn_byte(e, 0x67u); dyn_byte(e, reg + 1u); dyn_byte(e, reg);
            }
            dyn_constant(e, reg, 0u);
        } else e->error = 1;
        return;
    }
    if (node->kind == DYN_NODE_CALL) {
        unsigned int count = 0;
        unsigned int item = node->left;
        unsigned int wanted;
        while (item != DYN_INVALID_NODE) {
            if (item >= program->count || program->nodes[item].kind != DYN_NODE_ARGUMENT) {
                e->error = 1; return;
            }
            count += 1u; item = program->nodes[item].right;
        }
        if (count > 6u || node->value >= program->function_count) {
            e->error = 1; return;
        }
        item = node->left;
        while (item != DYN_INVALID_NODE) {
            dyn_expression(e, program, program->nodes[item].left, 1u);
            dyn_push(e, 1u); item = program->nodes[item].right;
        }
        wanted = count;
        while (wanted) {
            dyn_pop(e, wanted);
            wanted -= 1u;
        }
        dyn_call(e, node->value);
        if (reg != 1u) dyn_move(e, reg, 1u);
        return;
    }
    if (node->kind == DYN_NODE_ASSIGN) {
        unsigned int size;
        dyn_expression(e, program, node->right, reg);
        if (node->left >= program->count
            || (program->nodes[node->left].kind != DYN_NODE_LOCAL
                && program->nodes[node->left].kind != DYN_NODE_DEREFERENCE
                && program->nodes[node->left].kind != DYN_NODE_SUBSCRIPT
                && program->nodes[node->left].kind != DYN_NODE_GLOBAL
                && program->nodes[node->left].kind != DYN_NODE_MEMBER))
            e->error = 1;
        else {
            dyn_push(e, reg);
            dyn_lvalue_address(e, program, node->left, reg + 1u);
            dyn_pop(e, reg);
            size = dyn_lvalue_size(program, node->left);
            dyn_byte(e, dyn_memory_operation(size, 1));
            dyn_byte(e, reg); dyn_byte(e, reg + 1u);
        }
        return;
    }
    if (node->kind == DYN_NODE_ADDRESS) {
        dyn_lvalue_address(e, program, node->left, reg); return;
    }
    if (node->kind == DYN_NODE_DEREFERENCE || node->kind == DYN_NODE_SUBSCRIPT) {
        dyn_lvalue_address(e, program, index, reg);
        dyn_byte(e, dyn_memory_operation(node->value, 0));
        dyn_byte(e, reg << 4); dyn_byte(e, reg); return;
    }
    if (node->kind == DYN_NODE_POST_INCREMENT) {
        unsigned int local;
        unsigned int temporary = reg + 1u;
        if (node->left >= program->count || temporary >= 7u
            || program->nodes[node->left].kind != DYN_NODE_LOCAL) {
            e->error = 1; return;
        }
        local = program->nodes[node->left].value;
        dyn_expression(e, program, node->left, reg);
        dyn_alu_immediate(
            e, node->value ? 0x24u : 0x25u, temporary, reg,
            program->locals[local].pointer
                ? program->locals[local].element_size : 1u
        );
        dyn_local_address(e, local, 7u);
        dyn_byte(e, dyn_memory_operation(program->locals[local].size, 1));
        dyn_byte(e, temporary); dyn_byte(e, 7u);
        return;
    }
    if (node->kind == DYN_NODE_COMMA) {
        dyn_expression(e, program, node->left, reg);
        dyn_expression(e, program, node->right, reg); return;
    }
    if (node->kind == DYN_NODE_CONDITIONAL) {
        dyn_expression(e, program, node->value, reg);
        dyn_compare_zero(e, reg); fixup = dyn_branch(e, 0x41u);
        dyn_expression(e, program, node->left, reg);
        done = dyn_branch(e, 0x48u); dyn_patch(e, fixup, e->position);
        dyn_expression(e, program, node->right, reg);
        dyn_patch(e, done, e->position); return;
    }
    if (node->kind == DYN_NODE_POSITIVE || node->kind == DYN_NODE_NEGATIVE
        || node->kind == DYN_NODE_NOT || node->kind == DYN_NODE_LOGICAL_NOT) {
        dyn_expression(e, program, node->left, reg);
        if (node->kind == DYN_NODE_NEGATIVE) dyn_alu(e, 0x25u, reg, 0u, reg);
        else if (node->kind == DYN_NODE_NOT) dyn_alu(e, 0x20u, reg, reg, reg);
        else if (node->kind == DYN_NODE_LOGICAL_NOT) {
            dyn_compare_zero(e, reg); dyn_boolean(e, reg, 0x41u);
        }
        return;
    }
    if (node->kind == DYN_NODE_LOGICAL_AND || node->kind == DYN_NODE_LOGICAL_OR) {
        dyn_expression(e, program, node->left, reg); dyn_compare_zero(e, reg);
        fixup = dyn_branch(e, node->kind == DYN_NODE_LOGICAL_AND ? 0x41u : 0x49u);
        dyn_expression(e, program, node->right, reg); dyn_compare_zero(e, reg);
        dyn_boolean(e, reg, 0x49u); done = dyn_branch(e, 0x48u);
        dyn_patch(e, fixup, e->position);
        dyn_constant(e, reg, node->kind == DYN_NODE_LOGICAL_AND ? 0u : 1u);
        dyn_patch(e, done, e->position); return;
    }
    dyn_expression(e, program, node->left, reg);
    dyn_push(e, reg);
    dyn_expression(e, program, node->right, reg + 1u);
    dyn_pop(e, reg);
    if (node->kind == DYN_NODE_ADD || node->kind == DYN_NODE_SUBTRACT) {
        unsigned int scale = dyn_pointer_element(program, node->left);
        unsigned int right_scale = dyn_pointer_element(program, node->right);
        if (scale && right_scale && node->kind == DYN_NODE_ADD)
            e->error = 1;
        else if (!scale && right_scale && node->kind == DYN_NODE_SUBTRACT)
            e->error = 1;
        else if (!right_scale && scale == 2u)
            dyn_alu_immediate(e, 0x27u, reg + 1u, reg + 1u, 1u);
        else if (!right_scale && scale == 4u)
            dyn_alu_immediate(e, 0x27u, reg + 1u, reg + 1u, 2u);
        else if (!right_scale && scale != 0u && scale != 1u) {
            dyn_constant(e, reg + 2u, scale);
            dyn_multiply(e, reg + 1u);
        }
        else if (!scale && right_scale == 2u)
            dyn_alu_immediate(e, 0x27u, reg, reg, 1u);
        else if (!scale && right_scale == 4u)
            dyn_alu_immediate(e, 0x27u, reg, reg, 2u);
        else if (!scale && right_scale != 0u && right_scale != 1u) {
            dyn_push(e, reg + 1u);
            dyn_constant(e, reg + 1u, right_scale);
            dyn_multiply(e, reg);
            dyn_pop(e, reg + 1u);
        }
    }
    if (node->kind == DYN_NODE_MULTIPLY) { dyn_multiply(e, reg); return; }
    if (node->kind == DYN_NODE_DIVIDE) { dyn_divide(e, reg, 0); return; }
    if (node->kind == DYN_NODE_REMAINDER) { dyn_divide(e, reg, 1); return; }
    if (node->kind == DYN_NODE_ADD) operation = 0x24u;
    else if (node->kind == DYN_NODE_SUBTRACT) operation = 0x25u;
    else if (node->kind == DYN_NODE_LSHIFT) operation = 0x27u;
    else if (node->kind == DYN_NODE_RSHIFT) operation = 0x28u;
    else if (node->kind == DYN_NODE_AND) operation = 0x22u;
    else if (node->kind == DYN_NODE_OR) operation = 0x21u;
    else if (node->kind == DYN_NODE_XOR) operation = 0x26u;
    if (operation) {
        dyn_alu(e, operation, reg, reg, reg + 1u);
        if (node->kind == DYN_NODE_SUBTRACT
            && dyn_pointer_element(program, node->left)
            && dyn_pointer_element(program, node->right)) {
            unsigned int scale = dyn_pointer_element(program, node->left);
            if (scale == 2u) dyn_alu_immediate(e, 0x28u, reg, reg, 1u);
            else if (scale == 4u) dyn_alu_immediate(e, 0x28u, reg, reg, 2u);
            else if (scale != 1u) {
                dyn_constant(e, reg + 1u, scale);
                dyn_divide(e, reg, 0);
            }
        }
        return;
    }
    dyn_byte(e, 0x2au); dyn_byte(e, reg); dyn_byte(e, reg + 1u);
    if (node->kind == DYN_NODE_EQUAL) operation = 0x41u;
    else if (node->kind == DYN_NODE_NOT_EQUAL) operation = 0x49u;
    else if (node->kind == DYN_NODE_LESS) operation = 0x42u;
    else if (node->kind == DYN_NODE_GREATER) operation = 0x4bu;
    else if (node->kind == DYN_NODE_LESS_EQUAL) operation = 0x43u;
    else if (node->kind == DYN_NODE_GREATER_EQUAL) operation = 0x4au;
    if (operation) dyn_boolean(e, reg, operation); else e->error = 1;
}

static void dyn_statement(struct DynEmitter *e,
                          const struct DynAstProgram *program,
                          unsigned int index) {
    const struct DynNode *node;
    unsigned int branch;
    unsigned int done;
    unsigned int top;
    unsigned int update;
    unsigned int break_start;
    unsigned int continue_start;
    unsigned int index2;
    if (index == DYN_INVALID_NODE) return;
    if (index >= program->count) { e->error = 1; return; }
    node = &program->nodes[index];
    if (node->kind == DYN_NODE_SEQUENCE) {
        dyn_statement(e, program, node->left); dyn_statement(e, program, node->right);
    } else if (node->kind == DYN_NODE_RETURN) {
        dyn_expression(e, program, node->left, 1u);
        if (e->return_count >= e->return_capacity) { e->error = 1; return; }
        e->returns[e->return_count] = dyn_branch(e, 0x48u); e->return_count += 1u;
    } else if (node->kind == DYN_NODE_IF) {
        dyn_expression(e, program, node->value, 1u); dyn_compare_zero(e, 1u);
        branch = dyn_branch(e, 0x41u); dyn_statement(e, program, node->left);
        if (node->right != DYN_INVALID_NODE) {
            done = dyn_branch(e, 0x48u); dyn_patch(e, branch, e->position);
            dyn_statement(e, program, node->right); dyn_patch(e, done, e->position);
        } else dyn_patch(e, branch, e->position);
    } else if (node->kind == DYN_NODE_WHILE) {
        break_start = e->break_count; continue_start = e->continue_count;
        e->loop_depth += 1u;
        top = e->position; dyn_expression(e, program, node->left, 1u);
        dyn_compare_zero(e, 1u); done = dyn_branch(e, 0x41u);
        dyn_statement(e, program, node->right);
        index2 = continue_start;
        while (index2 < e->continue_count) {
            dyn_patch(e, e->continues[index2], top); index2 += 1u;
        }
        branch = dyn_branch(e, 0x48u); dyn_patch(e, branch, top);
        dyn_patch(e, done, e->position);
        index2 = break_start;
        while (index2 < e->break_count) {
            dyn_patch(e, e->breaks[index2], e->position); index2 += 1u;
        }
        e->break_count = break_start; e->continue_count = continue_start;
        e->loop_depth -= 1u;
    } else if (node->kind == DYN_NODE_DO) {
        break_start = e->break_count; continue_start = e->continue_count;
        e->loop_depth += 1u; top = e->position;
        dyn_statement(e, program, node->left); update = e->position;
        index2 = continue_start;
        while (index2 < e->continue_count) {
            dyn_patch(e, e->continues[index2], update); index2 += 1u;
        }
        dyn_expression(e, program, node->right, 1u); dyn_compare_zero(e, 1u);
        branch = dyn_branch(e, 0x49u); dyn_patch(e, branch, top);
        index2 = break_start;
        while (index2 < e->break_count) {
            dyn_patch(e, e->breaks[index2], e->position); index2 += 1u;
        }
        e->break_count = break_start; e->continue_count = continue_start;
        e->loop_depth -= 1u;
    } else if (node->kind == DYN_NODE_FOR) {
        if (node->value != DYN_INVALID_NODE) {
            if (program->nodes[node->value].kind == DYN_NODE_ASSIGN)
                dyn_statement(e, program, node->value);
            else dyn_expression(e, program, node->value, 1u);
        }
        break_start = e->break_count; continue_start = e->continue_count;
        e->loop_depth += 1u; top = e->position;
        dyn_expression(e, program, node->left, 1u); dyn_compare_zero(e, 1u);
        done = dyn_branch(e, 0x41u); dyn_statement(e, program, node->right);
        update = e->position;
        index2 = continue_start;
        while (index2 < e->continue_count) {
            dyn_patch(e, e->continues[index2], update); index2 += 1u;
        }
        if (node->extra != DYN_INVALID_NODE)
            dyn_expression(e, program, node->extra, 1u);
        branch = dyn_branch(e, 0x48u); dyn_patch(e, branch, top);
        dyn_patch(e, done, e->position); index2 = break_start;
        while (index2 < e->break_count) {
            dyn_patch(e, e->breaks[index2], e->position); index2 += 1u;
        }
        e->break_count = break_start; e->continue_count = continue_start;
        e->loop_depth -= 1u;
    } else if (node->kind == DYN_NODE_BREAK || node->kind == DYN_NODE_CONTINUE) {
        unsigned int *items;
        unsigned int *count;
        if (!e->loop_depth) { e->error = 1; return; }
        items = node->kind == DYN_NODE_BREAK ? e->breaks : e->continues;
        count = node->kind == DYN_NODE_BREAK ? &e->break_count : &e->continue_count;
        if (*count >= e->return_capacity) { e->error = 1; return; }
        items[*count] = dyn_branch(e, 0x48u); *count += 1u;
    } else if (node->kind == DYN_NODE_EXPRESSION || node->kind == DYN_NODE_ASSIGN)
        dyn_expression(e, program,
            node->kind == DYN_NODE_EXPRESSION ? node->left : index, 1u);
    else e->error = 1;
}

int dyn_emit_image(const struct DynIrModule *module, unsigned int load_address,
                   char *output, unsigned int capacity, unsigned int *length) {
    struct DynEmitter e;
    unsigned int index;
    unsigned int halt;
    unsigned int function_index;
    e.program = module->program;
    e.output = output; e.capacity = capacity; e.position = 0;
    e.load_address = load_address; e.return_capacity = module->program->count + 1u;
    e.returns = calloc(e.return_capacity, sizeof(unsigned int));
    e.breaks = calloc(e.return_capacity, sizeof(unsigned int));
    e.continues = calloc(e.return_capacity, sizeof(unsigned int));
    e.function_offsets = calloc(
        module->program->function_count, sizeof(unsigned int)
    );
    e.call_fixups = calloc(e.return_capacity, sizeof(unsigned int));
    e.call_targets = calloc(e.return_capacity, sizeof(unsigned int));
    e.global_offsets = calloc(
        module->program->global_count + 1u, sizeof(unsigned int)
    );
    e.global_fixups = calloc(e.return_capacity, sizeof(unsigned int));
    e.global_targets = calloc(e.return_capacity, sizeof(unsigned int));
    e.return_count = 0; e.break_count = 0; e.continue_count = 0;
    e.call_count = 0; e.global_fixup_count = 0;
    e.loop_depth = 0; e.current_local_base = 0;
    e.error = e.returns && e.breaks && e.continues && e.function_offsets
        && e.call_fixups && e.call_targets && e.global_offsets
        && e.global_fixups && e.global_targets ? 0 : 1;
    if (module->constant) {
        dyn_constant(&e, 1u, module->return_value);
        if (load_address + 12u <= 65535u) {
            dyn_byte(&e, 0x58u); dyn_byte(&e, 0x0fu);
            dyn_u16(&e, load_address + 12u);
        } else {
            dyn_constant(&e, 7u, load_address + 24u);
            dyn_byte(&e, 0x48u); dyn_byte(&e, 0x0fu); dyn_byte(&e, 0x07u);
        }
        *length = e.position;
        free(e.global_targets); free(e.global_fixups); free(e.global_offsets);
        free(e.call_targets); free(e.call_fixups); free(e.function_offsets);
        free(e.continues); free(e.breaks); free(e.returns);
        return e.error ? 0 : 1;
    }
    dyn_alu_immediate(&e, 0x21u, 14u, 0u, 0u);
    dyn_call(&e, module->program->main_function);
    halt = e.position;
    dyn_constant(&e, 7u, load_address + halt + 12u);
    dyn_byte(&e, 0x48u); dyn_byte(&e, 0x0fu); dyn_byte(&e, 0x07u);
    function_index = 0;
    while (function_index < module->program->function_count) {
        const struct DynFunction *function =
            &module->program->functions[function_index];
        unsigned int return_start;
        unsigned int epilogue;
        unsigned int frame_size;
        if (function->body == DYN_INVALID_NODE) {
            function_index += 1u;
            continue;
        }
        e.function_offsets[function_index] = e.position;
        e.current_local_base = function->local_base;
        dyn_push(&e, 12u);
        dyn_move(&e, 12u, 14u);
        frame_size = function->frame_size;
        if (frame_size <= 65535u)
            dyn_alu_immediate(&e, 0x25u, 14u, 14u, frame_size);
        else {
            dyn_constant(&e, 7u, frame_size);
            dyn_alu(&e, 0x25u, 14u, 14u, 7u);
        }
        index = 0;
        while (index < function->parameter_count) {
            if (index >= 6u) { e.error = 1; break; }
            dyn_local_address(&e, function->local_base + index, 7u);
            dyn_byte(&e, dyn_memory_operation(
                module->program->locals[function->local_base + index].size, 1
            ));
            dyn_byte(&e, index + 1u); dyn_byte(&e, 7u);
            index += 1u;
        }
        return_start = e.return_count;
        dyn_statement(&e, module->program, function->body);
        dyn_constant(&e, 1u, 0u);
        epilogue = e.position;
        index = return_start;
        while (index < e.return_count) {
            dyn_patch(&e, e.returns[index], epilogue); index += 1u;
        }
        e.return_count = return_start;
        dyn_move(&e, 14u, 12u);
        dyn_pop(&e, 12u);
        dyn_return_instruction(&e);
        function_index += 1u;
    }
    index = 0;
    while (index < e.call_count) {
        if (e.call_targets[index] >= module->program->function_count)
            e.error = 1;
        else dyn_patch(
            &e, e.call_fixups[index], e.function_offsets[e.call_targets[index]]
        );
        index += 1u;
    }
    index = 0;
    while (index < module->program->global_count) {
        const struct DynGlobal *global = &module->program->globals[index];
        unsigned int bytes;
        unsigned int written = 0;
        unsigned int alignment = global->size;
        if (!global->defined) { index += 1u; continue; }
        if (global->struct_id != DYN_INVALID_NODE && !global->pointer)
            alignment = module->program->structs[global->struct_id].alignment;
        if (alignment > 4u) alignment = 4u;
        while (alignment > 1u && (e.position & (alignment - 1u)))
            dyn_byte(&e, 0u);
        e.global_offsets[index] = e.position;
        bytes = global->array ? global->element_size * global->count : global->size;
        while (written < bytes) {
            unsigned int value = global->data
                ? ((unsigned int)global->data[written]) & 255u
                : global->array || (global->struct_id != DYN_INVALID_NODE
                    && !global->pointer) ? 0u : global->initial_value;
            if (global->data) {
                dyn_byte(&e, value);
                written += 1u;
                continue;
            }
            if (global->array || (global->struct_id != DYN_INVALID_NODE
                && !global->pointer)) {
                dyn_byte(&e, 0u);
                written += 1u;
                continue;
            }
            unsigned int shift = 8u * (global->size - 1u - (written % global->size));
            dyn_byte(&e, value >> shift);
            written += 1u;
        }
        index += 1u;
    }
    index = 0;
    while (index < module->program->global_count) {
        const struct DynGlobal *global = &module->program->globals[index];
        if (global->defined && global->pointer
            && global->initializer_node != DYN_INVALID_NODE) {
            unsigned int node_index = global->initializer_node;
            const struct DynNode *node = &module->program->nodes[node_index];
            if (node->kind == DYN_NODE_ADDRESS) {
                node_index = node->left;
                node = &module->program->nodes[node_index];
            }
            if (node->kind != DYN_NODE_GLOBAL
                || node->value >= module->program->global_count)
                e.error = 1;
            else dyn_write_u32_at(
                &e, e.global_offsets[index],
                load_address + e.global_offsets[node->value]
            );
        }
        index += 1u;
    }
    index = 0;
    while (index < e.global_fixup_count) {
        dyn_constant_at(
            &e, e.global_fixups[index], 7u,
            load_address + e.global_offsets[e.global_targets[index]]
        );
        index += 1u;
    }
    *length = e.position;
    free(e.global_targets); free(e.global_fixups); free(e.global_offsets);
    free(e.call_targets); free(e.call_fixups); free(e.function_offsets);
    free(e.continues); free(e.breaks); free(e.returns);
    return e.error ? 0 : 1;
}
