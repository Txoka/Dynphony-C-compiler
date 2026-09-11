struct Arena {
    unsigned char *base;
    unsigned int capacity;
    unsigned int used;
};

void memory_set(void *destination, unsigned char value, unsigned int count) {
    unsigned char *bytes = destination;
    unsigned int index = 0;
    while (index < count) {
        bytes[index] = value;
        index++;
    }
}

void memory_copy(void *destination, const void *source, unsigned int count) {
    unsigned char *to = destination;
    const unsigned char *from = source;
    unsigned int index = 0;
    while (index < count) {
        to[index] = from[index];
        index++;
    }
}

void arena_init(struct Arena *arena, void *memory, unsigned int capacity) {
    arena->base = memory;
    arena->capacity = capacity;
    arena->used = 0;
}

void *arena_alloc(
    struct Arena *arena,
    unsigned int size,
    unsigned int alignment
) {
    unsigned int start =
        (arena->used + alignment - 1) & ~(alignment - 1);
    if (start + size > arena->capacity) {
        return 0;
    }
    arena->used = start + size;
    return arena->base + start;
}

int main(void) {
    unsigned char memory[64];
    struct Arena arena;
    int *first;
    int *second;

    arena_init(&arena, memory, sizeof(memory));
    first = arena_alloc(&arena, sizeof(int), 4);
    second = arena_alloc(&arena, sizeof(int), 4);
    *first = 0x12345678;
    memory_copy(second, first, sizeof(int));
    memory_set(first, 0, sizeof(int));

    return *first == 0 && *second == 0x12345678 && arena.used == 8;
}
