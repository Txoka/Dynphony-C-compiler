#include <stdlib.h>
#include "dynphony/compiler.h"

int main(void) {
    unsigned int length = input();
    unsigned int index = 0;
    char *source;
    int status;
    if (length > 1048576u) return DYN_COMPILE_INPUT_TOO_LARGE;
    source = malloc(length + 1u);
    if (!source) return DYN_COMPILE_OUT_OF_MEMORY;
    while (index < length) {
        source[index] = (char)input();
        index += 1;
    }
    source[length] = 0;
    status = dyn_compile_buffer(source, length);
    free(source);
    return status;
}
