#ifndef SYMPHONY_STDLIB_H
#define SYMPHONY_STDLIB_H

void *malloc(unsigned int size);
void free(void *pointer);
void *calloc(unsigned int count, unsigned int size);
void *realloc(void *pointer, unsigned int size);

#endif
