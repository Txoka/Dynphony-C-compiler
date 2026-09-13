#include <stdlib.h>
#include <dynphony.h>

extern unsigned char __dyn_heap_anchor[7];
static unsigned char *__dyn_heap_end;

void *malloc(unsigned int size) {
    unsigned char *result;
    if (!size) return 0;
    if (size > 0xfffffffcu) return 0;
    size = (size + 3u) & 0xfffffffcu;
    if (!__dyn_heap_end)
        __dyn_heap_end = (unsigned char *)(
            ((unsigned int)(__dyn_heap_anchor + 7u) + 3u) & 0xfffffffcu
        );
    if (dynphony_heap_remaining(__dyn_heap_end) < size) return 0;
    result = __dyn_heap_end;
    __dyn_heap_end += size;
    return result;
}

void free(void *pointer) {
    return;
}

void *calloc(unsigned int count, unsigned int size) {
    unsigned int total;
    unsigned int index = 0;
    unsigned char *result;
    if (count && size > 0xffffffffu / count) return 0;
    total = count * size;
    result = malloc(total);
    if (!result) return 0;
    while (index < total) {
        result[index] = 0;
        index += 1u;
    }
    return result;
}

void *realloc(void *pointer, unsigned int size) {
    if (!pointer) return malloc(size);
    if (!size) return 0;
    return 0;
}
