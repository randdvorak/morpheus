#ifndef MORPHEUS_LEAKCHECK_H
#define MORPHEUS_LEAKCHECK_H

#include <stddef.h>
#include <stdlib.h>

void *morph_runtime_leakcheck_malloc(
    size_t size,
    const char *source_file,
    int source_line);
void *morph_runtime_leakcheck_calloc(
    size_t count,
    size_t size,
    const char *source_file,
    int source_line);
void *morph_runtime_leakcheck_realloc(
    void *pointer,
    size_t size,
    const char *source_file,
    int source_line);
void morph_runtime_leakcheck_free(void *pointer);

#define malloc(size) \
    morph_runtime_leakcheck_malloc((size), __FILE__, __LINE__)
#define calloc(count, size) \
    morph_runtime_leakcheck_calloc((count), (size), __FILE__, __LINE__)
#define realloc(pointer, size) \
    morph_runtime_leakcheck_realloc((pointer), (size), __FILE__, __LINE__)
#define free(pointer) morph_runtime_leakcheck_free((pointer))

#endif
