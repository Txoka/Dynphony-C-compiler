#include "dynphony/middle.h"

static int dyn_constant_expression(
    const struct DynAstProgram *program,
    unsigned int index
) {
    const struct DynNode *node;
    if (index >= program->count) return 0;
    node = &program->nodes[index];
    if (node->kind == DYN_NODE_NUMBER) return 1;
    if (node->kind == DYN_NODE_LOCAL || node->kind == DYN_NODE_ASSIGN
        || node->kind == DYN_NODE_POST_INCREMENT
        || node->kind == DYN_NODE_CALL_INPUT
        || node->kind == DYN_NODE_CALL_OUTPUT
        || node->kind == DYN_NODE_CALL_INTRINSIC) return 0;
    if (node->kind == DYN_NODE_CONDITIONAL)
        return dyn_constant_expression(program, node->value)
            && dyn_constant_expression(program, node->left)
            && dyn_constant_expression(program, node->right);
    if (node->right == 0xffffffffu)
        return dyn_constant_expression(program, node->left);
    return dyn_constant_expression(program, node->left)
        && dyn_constant_expression(program, node->right);
}

int dyn_lower(
    const struct DynAstProgram *program,
    struct DynIrModule *module
) {
    module->program = program;
    module->constant = 0;
    if (
        program->function_count == 1u
        && program->expression < program->count
        && program->nodes[program->expression].kind == DYN_NODE_RETURN
        && dyn_constant_expression(
            program, program->nodes[program->expression].left
        )
    ) {
        module->constant = 1;
        return dyn_evaluate(
            program,
            program->nodes[program->expression].left,
            &module->return_value
        );
    }
    return 1;
}
