#include <symphony.h>
#include "symphony/compiler.h"

static void dyn_run_compiled_program(
    unsigned int output_address,
    unsigned int load_address
) {
    unsigned int length = persistent_load(output_address);
    unsigned int source = output_address + 4u;
    unsigned int copied = 0u;
    unsigned int *destination = (unsigned int *)load_address;
    while (copied < length) {
        *destination = persistent_load(source);
        destination += 1u;
        source += 4u;
        copied += 4u;
    }
    jump(load_address);
}

int main(void) {
    unsigned int persistent_size;
    unsigned int project_address;
    unsigned int project_length;
    unsigned int load_address;
    unsigned int output_address;
    unsigned int output_capacity;
    unsigned int status;
    unsigned int mode;
    if (persistent_load(0u) != 0x44434331u) return DYN_COMPILE_INVALID_CONTROL;
    if (persistent_load(4u) != 1u) return DYN_COMPILE_INVALID_CONTROL;
    persistent_size = persistent_load(8u);
    project_address = persistent_load(12u);
    project_length = persistent_load(16u);
    load_address = persistent_load(20u);
    output_address = persistent_load(24u);
    output_capacity = persistent_load(28u);
    mode = persistent_load(40u);
    if (
        persistent_size < 64u
        || (persistent_size & (persistent_size - 1u)) != 0u
        || (project_address & 3u) != 0u
        || (output_address & 3u) != 0u
        || project_address < 44u
        || project_address > persistent_size
        || project_length > persistent_size - project_address
        || output_address > persistent_size
        || output_capacity > persistent_size - output_address
        || output_address < project_address + project_length
        || mode > 3u
    ) {
        persistent_store(36u, 0u);
        persistent_store(32u, DYN_COMPILE_INVALID_CONTROL);
        return DYN_COMPILE_INVALID_CONTROL;
    }
    persistent_store(32u, 0xfffffffeu);
    status = (unsigned int)dyn_compile_project(
        project_address,
        project_length,
        load_address,
        mode >> 1u,
        output_address,
        output_capacity
    );
    persistent_store(36u, dyn_last_output_length());
    persistent_store(32u, status);
    if (!status && (mode & 1u) != 0u)
        dyn_run_compiled_program(output_address, load_address);
    return (int)status;
}
