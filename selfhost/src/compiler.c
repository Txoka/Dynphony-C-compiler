#include <stdlib.h>
#include "dynphony/compiler.h"
#include "dynphony/frontend.h"
#include "dynphony/middle.h"
#include "dynphony/target.h"

int dyn_compile_buffer(const char *source, unsigned int length) {
    struct DynAstProgram program;
    struct DynIrModule module;
    unsigned int capacity;
    int status = DYN_COMPILE_OK;

    if (length > 1048576u) return DYN_COMPILE_INPUT_TOO_LARGE;
    capacity = length + 1u;
    program.nodes = calloc(capacity, sizeof(struct DynNode));
    if (!program.nodes) return DYN_COMPILE_OUT_OF_MEMORY;
    program.capacity = capacity;

    if (!dyn_parse(source, length, &program))
        status = DYN_COMPILE_PARSE_ERROR;
    else if (!dyn_lower(&program, &module))
        status = DYN_COMPILE_SEMANTIC_ERROR;
    else {
        dyn_optimize(&module);
        dyn_emit_image(&module);
    }
    free(program.nodes);
    return status;
}
