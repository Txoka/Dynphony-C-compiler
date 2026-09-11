#include "dynphony/middle.h"

void dyn_optimize(struct DynIrModule *module) {
    /* The stage-0 IR is already a single folded return value. */
    module->return_value = module->return_value;
}
