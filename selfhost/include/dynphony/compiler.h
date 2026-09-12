#ifndef DYNPHONY_COMPILER_H
#define DYNPHONY_COMPILER_H

enum DynCompileStatus {
    DYN_COMPILE_OK,
    DYN_COMPILE_INPUT_TOO_LARGE,
    DYN_COMPILE_OUT_OF_MEMORY,
    DYN_COMPILE_LEX_ERROR,
    DYN_COMPILE_PARSE_ERROR,
    DYN_COMPILE_SEMANTIC_ERROR,
    DYN_COMPILE_INVALID_CONTROL,
    DYN_COMPILE_INVALID_PROJECT,
    DYN_COMPILE_OUTPUT_TOO_SMALL
};

int dyn_compile_buffer(
    const char *source,
    unsigned int length,
    unsigned int load_address,
    char *output,
    unsigned int output_capacity,
    unsigned int *output_length
);
int dyn_compile_project(
    unsigned int project_address,
    unsigned int project_byte_length,
    unsigned int program_load_address,
    unsigned int output_address,
    unsigned int output_capacity
);
unsigned int dyn_last_output_length(void);

#endif
