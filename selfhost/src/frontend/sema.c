#include "dynphony/frontend.h"

static int dyn_evaluate_node(
    const struct DynAstProgram *program,
    unsigned int index,
    unsigned int depth,
    unsigned int *result
) {
    const struct DynNode *node;
    unsigned int left;
    unsigned int right;
    if (index >= program->count || depth > program->count) return 0;
    node = &program->nodes[index];
    if (node->kind == DYN_NODE_NUMBER) {
        *result = node->value;
        return 1;
    }
    if (node->kind == DYN_NODE_CONDITIONAL) {
        if (!dyn_evaluate_node(program, node->value, depth + 1, &left)) return 0;
        return dyn_evaluate_node(
            program,
            left ? node->left : node->right,
            depth + 1,
            result
        );
    }
    if (!dyn_evaluate_node(program, node->left, depth + 1, &left)) return 0;
    if (node->kind == DYN_NODE_POSITIVE) *result = left;
    else if (node->kind == DYN_NODE_NEGATIVE) *result = 0u - left;
    else if (node->kind == DYN_NODE_NOT) *result = ~left;
    else if (node->kind == DYN_NODE_LOGICAL_NOT) *result = !left;
    else if (node->kind == DYN_NODE_LOGICAL_AND) {
        if (!left) {
            *result = 0;
            return 1;
        }
        if (!dyn_evaluate_node(program, node->right, depth + 1, &right)) return 0;
        *result = right != 0;
    } else if (node->kind == DYN_NODE_LOGICAL_OR) {
        if (left) {
            *result = 1;
            return 1;
        }
        if (!dyn_evaluate_node(program, node->right, depth + 1, &right)) return 0;
        *result = right != 0;
    } else {
        if (!dyn_evaluate_node(program, node->right, depth + 1, &right)) return 0;
        if (
            (node->kind == DYN_NODE_DIVIDE
                || node->kind == DYN_NODE_REMAINDER)
            && right == 0
        ) return 0;
        if (
            (node->kind == DYN_NODE_LSHIFT
                || node->kind == DYN_NODE_RSHIFT)
            && right >= 32
        ) return 0;
        if (node->kind == DYN_NODE_ADD) *result = left + right;
        else if (node->kind == DYN_NODE_SUBTRACT) *result = left - right;
        else if (node->kind == DYN_NODE_MULTIPLY) *result = left * right;
        else if (node->kind == DYN_NODE_DIVIDE) *result = left / right;
        else if (node->kind == DYN_NODE_REMAINDER) *result = left % right;
        else if (node->kind == DYN_NODE_LSHIFT) *result = left << right;
        else if (node->kind == DYN_NODE_RSHIFT) *result = left >> right;
        else if (node->kind == DYN_NODE_AND) *result = left & right;
        else if (node->kind == DYN_NODE_OR) *result = left | right;
        else if (node->kind == DYN_NODE_XOR) *result = left ^ right;
        else if (node->kind == DYN_NODE_EQUAL) *result = left == right;
        else if (node->kind == DYN_NODE_NOT_EQUAL) *result = left != right;
        else if (node->kind == DYN_NODE_LESS) *result = left < right;
        else if (node->kind == DYN_NODE_GREATER) *result = left > right;
        else if (node->kind == DYN_NODE_LESS_EQUAL) *result = left <= right;
        else if (node->kind == DYN_NODE_GREATER_EQUAL) *result = left >= right;
        else if (node->kind == DYN_NODE_COMMA) *result = right;
        else return 0;
    }
    return 1;
}

int dyn_evaluate(
    const struct DynAstProgram *program,
    unsigned int node,
    unsigned int *result
) {
    return dyn_evaluate_node(program, node, 0, result);
}
