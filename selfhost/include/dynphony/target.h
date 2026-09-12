#ifndef DYNPHONY_TARGET_H
#define DYNPHONY_TARGET_H

#include "dynphony/middle.h"

unsigned int dyn_image_size(void);
int dyn_emit_image(
    const struct DynIrModule *module,
    unsigned int load_address,
    char *output,
    unsigned int capacity
);

#endif
