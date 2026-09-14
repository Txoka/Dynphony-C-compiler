#ifndef SYMPHONY_TARGET_H
#define SYMPHONY_TARGET_H

#include "symphony/middle.h"

int dyn_emit_image(
    const struct DynIrModule *module,
    unsigned int load_address,
    unsigned int symphony,
    char *output,
    unsigned int capacity,
    unsigned int *length
);

#endif
