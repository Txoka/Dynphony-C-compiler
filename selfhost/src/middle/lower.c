#include "dynphony/middle.h"

int dyn_lower(
    const struct DynAstProgram *program,
    struct DynIrModule *module
) {
    return dyn_evaluate(program, program->expression, &module->return_value);
}
