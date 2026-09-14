#ifndef SYMPHONY_MIDDLE_H
#define SYMPHONY_MIDDLE_H

#include "symphony/frontend.h"

struct DynIrModule {
    const struct DynAstProgram *program;
    int constant;
    unsigned int return_value;
};

int dyn_lower(
    const struct DynAstProgram *program,
    struct DynIrModule *module
);
void dyn_optimize(struct DynIrModule *module);

#endif
