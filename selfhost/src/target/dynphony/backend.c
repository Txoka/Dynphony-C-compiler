#include "dynphony/target.h"

struct DynEmitter {
    char *output;
    unsigned int capacity;
    unsigned int position;
    int error;
};

static void dyn_emit_byte(struct DynEmitter *emitter, unsigned int value) {
    if (emitter->position >= emitter->capacity) {
        emitter->error = 1;
        return;
    }
    emitter->output[emitter->position] = (char)(value & 255u);
    emitter->position += 1;
}

static void dyn_emit_u16(struct DynEmitter *emitter, unsigned int value) {
    dyn_emit_byte(emitter, value >> 8);
    dyn_emit_byte(emitter, value);
}

static void dyn_materialize(
    struct DynEmitter *emitter,
    unsigned int reg,
    unsigned int value
) {
    dyn_emit_byte(emitter, 0x31u);
    dyn_emit_byte(emitter, reg << 4);
    dyn_emit_u16(emitter, value >> 16);
    dyn_emit_byte(emitter, 0x37u);
    dyn_emit_byte(emitter, (reg << 4) | reg);
    dyn_emit_u16(emitter, 16u);
    dyn_emit_byte(emitter, 0x31u);
    dyn_emit_byte(emitter, (reg << 4) | reg);
    dyn_emit_u16(emitter, value);
}

unsigned int dyn_image_size(void) {
    return 27u;
}

int dyn_emit_image(
    const struct DynIrModule *module,
    unsigned int load_address,
    char *output,
    unsigned int capacity
) {
    struct DynEmitter emitter;
    unsigned int value = module->return_value;
    emitter.output = output;
    emitter.capacity = capacity;
    emitter.position = 0;
    emitter.error = 0;

    dyn_materialize(&emitter, 1u, value);
    dyn_materialize(&emitter, 2u, load_address + 24u);
    /* jmp r2: repeat the final instruction forever. */
    dyn_emit_byte(&emitter, 0x48u);
    dyn_emit_byte(&emitter, 0x0fu);
    dyn_emit_byte(&emitter, 0x02u);
    return emitter.error ? 0 : 1;
}
