#ifndef MORPHEUS_RUNTIME_LEAKCHECK_H
#define MORPHEUS_RUNTIME_LEAKCHECK_H

#include <stddef.h>

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
size_t morph_runtime_leakcheck_live_allocations(void);
size_t morph_runtime_leakcheck_live_bytes(void);
size_t morph_runtime_leakcheck_report(void);

#endif
