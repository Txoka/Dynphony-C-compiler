#ifndef DYNPHONY_COMPILER_H
#define DYNPHONY_COMPILER_H

enum DynCompileStatus {
    DYN_COMPILE_OK,
    DYN_COMPILE_INPUT_TOO_LARGE,
    DYN_COMPILE_OUT_OF_MEMORY,
    DYN_COMPILE_LEX_ERROR,
    DYN_COMPILE_PARSE_ERROR,
    DYN_COMPILE_SEMANTIC_ERROR
};

int dyn_compile_buffer(const char *source, unsigned int length);

#endif
