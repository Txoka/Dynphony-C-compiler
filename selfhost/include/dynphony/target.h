#ifndef DYNPHONY_TARGET_H
#define DYNPHONY_TARGET_H

#include "dynphony/middle.h"

int dyn_emit_image(
    const struct DynIrModule *module,
    unsigned int load_address,
    unsigned int symphony,
    char *output,
    unsigned int capacity,
    unsigned int *length
);

#endif
