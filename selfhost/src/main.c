#include <dynphony.h>
#include "dynphony/compiler.h"

int main(void) {
    unsigned int persistent_size;
    unsigned int project_address;
    unsigned int project_length;
    unsigned int load_address;
    unsigned int output_address;
    unsigned int output_capacity;
    unsigned int status;
    if (persistent_load(0u) != 0x44434331u) return DYN_COMPILE_INVALID_CONTROL;
    if (persistent_load(4u) != 1u) return DYN_COMPILE_INVALID_CONTROL;
    persistent_size = persistent_load(8u);
    project_address = persistent_load(12u);
    project_length = persistent_load(16u);
    load_address = persistent_load(20u);
    output_address = persistent_load(24u);
    output_capacity = persistent_load(28u);
    if (
        persistent_size < 64u
        || (persistent_size & (persistent_size - 1u)) != 0u
        || (project_address & 3u) != 0u
        || (output_address & 3u) != 0u
        || project_address < 40u
        || project_address > persistent_size
        || project_length > persistent_size - project_address
        || output_address > persistent_size
        || output_capacity > persistent_size - output_address
        || output_address < project_address + project_length
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
        output_address,
        output_capacity
    );
    persistent_store(36u, dyn_last_output_length());
    persistent_store(32u, status);
    return (int)status;
}
