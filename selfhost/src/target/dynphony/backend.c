#include <dynphony.h>
#include "dynphony/target.h"

static void dyn_emit_byte(unsigned int value) {
    output(value & 255u);
}

static void dyn_emit_u16(unsigned int value) {
    dyn_emit_byte(value >> 8);
    dyn_emit_byte(value);
}

void dyn_emit_image(const struct DynIrModule *module) {
    unsigned int value = module->return_value;

    /* mov r1, high16 */
    dyn_emit_byte(0x31u);
    dyn_emit_byte(0x10u);
    dyn_emit_u16(value >> 16);

    /* lsl r1, r1, 16 */
    dyn_emit_byte(0x37u);
    dyn_emit_byte(0x11u);
    dyn_emit_u16(16u);

    /* or r1, r1, low16 */
    dyn_emit_byte(0x31u);
    dyn_emit_byte(0x11u);
    dyn_emit_u16(value);

    /* jmp 12: repeat the final instruction forever. */
    dyn_emit_byte(0x58u);
    dyn_emit_byte(0x0fu);
    dyn_emit_u16(12u);
}
