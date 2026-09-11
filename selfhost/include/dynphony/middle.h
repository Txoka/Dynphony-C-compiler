#ifndef DYNPHONY_MIDDLE_H
#define DYNPHONY_MIDDLE_H

#include "dynphony/frontend.h"

struct DynIrModule {
    unsigned int return_value;
};

int dyn_lower(
    const struct DynAstProgram *program,
    struct DynIrModule *module
);
void dyn_optimize(struct DynIrModule *module);

#endif
