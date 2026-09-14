#include <stdlib.h>
#include <string.h>
#include <symphony.h>

void *memcpy(void *destination, const void *source, unsigned int count) {
    unsigned char *to = destination;
    const unsigned char *from = source;
    unsigned int index = 0u;
    while (index < count) { to[index] = from[index]; index += 1u; }
    return destination;
}

void *memmove(void *destination, const void *source, unsigned int count) {
    unsigned char *to = destination;
    const unsigned char *from = source;
    if ((unsigned int)to < (unsigned int)from) {
        unsigned int index = 0u;
        while (index < count) { to[index] = from[index]; index += 1u; }
    } else if ((unsigned int)to > (unsigned int)from) {
        while (count) { count -= 1u; to[count] = from[count]; }
    }
    return destination;
}

void *memset(void *destination, int value, unsigned int count) {
    unsigned char *bytes = destination;
    unsigned int index = 0u;
    while (index < count) { bytes[index] = (unsigned char)value; index += 1u; }
    return destination;
}

int memcmp(const void *left, const void *right, unsigned int count) {
    const unsigned char *a = left;
    const unsigned char *b = right;
    unsigned int index = 0u;
    while (index < count) {
        if (a[index] != b[index]) return (int)a[index] - (int)b[index];
        index += 1u;
    }
    return 0;
}

struct DynHeapBlock { unsigned int size; struct DynHeapBlock *next; };

extern unsigned char __dyn_heap_anchor[7];
static unsigned char *__dyn_heap_end;
static struct DynHeapBlock *__dyn_heap_free_list;

static void *dyn_take_free_block(unsigned int size) {
    struct DynHeapBlock *previous = 0;
    struct DynHeapBlock *block = __dyn_heap_free_list;
    while (block) {
        if (block->size >= size) {
            unsigned int spare = block->size - size;
            if (spare >= sizeof(struct DynHeapBlock) + 4u) {
                struct DynHeapBlock *rest = (struct DynHeapBlock *)(
                    (unsigned char *)block + sizeof(struct DynHeapBlock) + size
                );
                rest->size = spare - sizeof(struct DynHeapBlock);
                rest->next = block->next;
                if (previous) previous->next = rest;
                else __dyn_heap_free_list = rest;
                block->size = size;
            } else {
                if (previous) previous->next = block->next;
                else __dyn_heap_free_list = block->next;
            }
            block->next = 0;
            return (unsigned char *)block + sizeof(struct DynHeapBlock);
        }
        previous = block;
        block = block->next;
    }
    return 0;
}

void *malloc(unsigned int size) {
    struct DynHeapBlock *block;
    void *reused;
    unsigned int required;
    if (!size || size > 0xfffffff4u) return 0;
    size = (size + 3u) & 0xfffffffcu;
    reused = dyn_take_free_block(size);
    if (reused) return reused;
    if (!__dyn_heap_end) __dyn_heap_end = (unsigned char *)(
        ((unsigned int)(__dyn_heap_anchor + 7u) + 3u) & 0xfffffffcu
    );
    required = sizeof(struct DynHeapBlock) + size;
    if (symphony_heap_remaining(__dyn_heap_end) < required) return 0;
    block = (struct DynHeapBlock *)__dyn_heap_end;
    block->size = size;
    block->next = 0;
    __dyn_heap_end += required;
    return (unsigned char *)block + sizeof(struct DynHeapBlock);
}

void free(void *pointer) {
    struct DynHeapBlock *block;
    struct DynHeapBlock *previous = 0;
    struct DynHeapBlock *next = __dyn_heap_free_list;
    if (!pointer) return;
    block = (struct DynHeapBlock *)(
        (unsigned char *)pointer - sizeof(struct DynHeapBlock)
    );
    while (next && (unsigned int)next < (unsigned int)block) {
        previous = next;
        next = next->next;
    }
    block->next = next;
    if (previous) previous->next = block;
    else __dyn_heap_free_list = block;
    if (next && (unsigned char *)block + sizeof(struct DynHeapBlock)
        + block->size == (unsigned char *)next) {
        block->size += sizeof(struct DynHeapBlock) + next->size;
        block->next = next->next;
    }
    if (previous && (unsigned char *)previous + sizeof(struct DynHeapBlock)
        + previous->size == (unsigned char *)block) {
        previous->size += sizeof(struct DynHeapBlock) + block->size;
        previous->next = block->next;
    }
}

void *calloc(unsigned int count, unsigned int size) {
    unsigned int total;
    void *pointer;
    if (count && size > 0xffffffffu / count) return 0;
    total = count * size;
    pointer = malloc(total);
    if (pointer) memset(pointer, 0, total);
    return pointer;
}

void *realloc(void *pointer, unsigned int size) {
    struct DynHeapBlock *block;
    void *replacement;
    unsigned int copy_size;
    if (!pointer) return malloc(size);
    if (!size) { free(pointer); return 0; }
    block = (struct DynHeapBlock *)(
        (unsigned char *)pointer - sizeof(struct DynHeapBlock)
    );
    if (block->size >= size) return pointer;
    replacement = malloc(size);
    if (!replacement) return 0;
    copy_size = block->size < size ? block->size : size;
    memcpy(replacement, pointer, copy_size);
    free(pointer);
    return replacement;
}
