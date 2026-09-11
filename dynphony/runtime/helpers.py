"""Freestanding C runtime, compiled through the normal frontend and backend.

Unsigned division retains the shifted-out bit, so divisors >= 2**31 work without
an unavailable 64-bit intermediate. Division by zero is C undefined behavior;
these helpers deterministically return 0.
"""

SOURCE = r"""
void *memcpy(void *destination, const void *source, unsigned int count) {
    unsigned char *to = destination;
    const unsigned char *from = source;
    unsigned int index = 0;
    while (index < count) {
        to[index] = from[index];
        index += 1;
    }
    return destination;
}

void *memmove(void *destination, const void *source, unsigned int count) {
    unsigned char *to = destination;
    const unsigned char *from = source;
    if ((unsigned int)to < (unsigned int)from) {
        unsigned int index = 0;
        while (index < count) {
            to[index] = from[index];
            index += 1;
        }
    } else if ((unsigned int)to > (unsigned int)from) {
        while (count) {
            count -= 1;
            to[count] = from[count];
        }
    }
    return destination;
}

void *memset(void *destination, int value, unsigned int count) {
    unsigned char *bytes = destination;
    unsigned int index = 0;
    while (index < count) {
        bytes[index] = (unsigned char)value;
        index += 1;
    }
    return destination;
}

int memcmp(const void *left, const void *right, unsigned int count) {
    const unsigned char *a = left;
    const unsigned char *b = right;
    unsigned int index = 0;
    while (index < count) {
        if (a[index] != b[index]) return (int)a[index] - (int)b[index];
        index += 1;
    }
    return 0;
}

struct __dyn_heap_block {
    unsigned int size;
    struct __dyn_heap_block *next;
};

extern unsigned char __dyn_heap_anchor[7];
static unsigned char *__dyn_heap_end;
static struct __dyn_heap_block *__dyn_heap_free_list;

void *__dyn_heap_take_free(unsigned int size) {
    struct __dyn_heap_block *previous = 0;
    struct __dyn_heap_block *block = __dyn_heap_free_list;
    while (block) {
        if (block->size >= size) {
            unsigned int spare = block->size - size;
            if (spare >= sizeof(struct __dyn_heap_block) + 4u) {
                struct __dyn_heap_block *rest =
                    (struct __dyn_heap_block *)((unsigned char *)block
                    + sizeof(struct __dyn_heap_block) + size);
                rest->size = spare - sizeof(struct __dyn_heap_block);
                rest->next = block->next;
                if (previous) previous->next = rest;
                else __dyn_heap_free_list = rest;
                block->size = size;
            } else {
                if (previous) previous->next = block->next;
                else __dyn_heap_free_list = block->next;
            }
            block->next = 0;
            return (unsigned char *)block + sizeof(struct __dyn_heap_block);
        }
        previous = block;
        block = block->next;
    }
    return 0;
}

void *malloc(unsigned int size) {
    struct __dyn_heap_block *block;
    void *reused;
    unsigned int required;
    if (!size) return 0;
    if (size > 0xfffffff4u) return 0;
    size = (size + 3u) & ~3u;
    reused = __dyn_heap_take_free(size);
    if (reused) return reused;
    if (!__dyn_heap_end) {
        __dyn_heap_end = (unsigned char *)(((unsigned int)
            (__dyn_heap_anchor + sizeof(__dyn_heap_anchor)) + 3u) & ~3u);
    }
    required = sizeof(struct __dyn_heap_block) + size;
    if (__dyn_heap_remaining(__dyn_heap_end) < required) return 0;
    block = (struct __dyn_heap_block *)__dyn_heap_end;
    block->size = size;
    block->next = 0;
    __dyn_heap_end += required;
    return (unsigned char *)block + sizeof(struct __dyn_heap_block);
}

void free(void *pointer) {
    struct __dyn_heap_block *block;
    struct __dyn_heap_block *previous = 0;
    struct __dyn_heap_block *next = __dyn_heap_free_list;
    if (!pointer) return;
    block = (struct __dyn_heap_block *)((unsigned char *)pointer
        - sizeof(struct __dyn_heap_block));
    while (next && (unsigned int)next < (unsigned int)block) {
        previous = next;
        next = next->next;
    }
    block->next = next;
    if (previous) previous->next = block;
    else __dyn_heap_free_list = block;
    if (next && (unsigned char *)block + sizeof(struct __dyn_heap_block)
            + block->size == (unsigned char *)next) {
        block->size += sizeof(struct __dyn_heap_block) + next->size;
        block->next = next->next;
    }
    if (previous && (unsigned char *)previous + sizeof(struct __dyn_heap_block)
            + previous->size == (unsigned char *)block) {
        previous->size += sizeof(struct __dyn_heap_block) + block->size;
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
    struct __dyn_heap_block *block;
    void *replacement;
    unsigned int copy_size;
    if (!pointer) return malloc(size);
    if (!size) { free(pointer); return 0; }
    block = (struct __dyn_heap_block *)((unsigned char *)pointer
        - sizeof(struct __dyn_heap_block));
    if (block->size >= size) return pointer;
    replacement = malloc(size);
    if (!replacement) return 0;
    copy_size = block->size < size ? block->size : size;
    memcpy(replacement, pointer, copy_size);
    free(pointer);
    return replacement;
}

unsigned int __dyn_mul(unsigned int a, unsigned int b) {
    unsigned int r=0;
    while (b) { if (b & 1u) r += a; a <<= 1; b >>= 1; }
    return r;
}
unsigned int __dyn_udivmod(unsigned int a, unsigned int b, int remainder) {
    unsigned int q=0, r=0;
    int i;
    if (!b) return 0;
    for (i=0; i<32; i++) {
        unsigned int carry=r >> 31;
        r=(r << 1) | (a >> 31);
        a <<= 1; q <<= 1;
        if (carry || r >= b) { r -= b; q |= 1u; }
    }
    return remainder ? r : q;
}
unsigned int __dyn_udiv(unsigned int a, unsigned int b) { return __dyn_udivmod(a,b,0); }
unsigned int __dyn_umod(unsigned int a, unsigned int b) { return __dyn_udivmod(a,b,1); }
int __dyn_sdiv(int a, int b) {
    unsigned int ua=(unsigned int)a, ub=(unsigned int)b, q;
    if (a<0) ua=0u-ua;
    if (b<0) ub=0u-ub;
    q=__dyn_udiv(ua,ub);
    if ((a<0) != (b<0)) q=0u-q;
    return (int)q;
}
int __dyn_smod(int a, int b) {
    unsigned int ua=(unsigned int)a, ub=(unsigned int)b, r;
    if (a<0) ua=0u-ua;
    if (b<0) ub=0u-ub;
    r=__dyn_umod(ua,ub);
    if (a<0) r=0u-r;
    return (int)r;
}
"""
