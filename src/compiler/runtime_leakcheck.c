#include "runtime_leakcheck.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define STB_LEAKCHECK_OUTPUT_PIPE stderr
#define STB_LEAKCHECK_IMPLEMENTATION
#include "stb_leakcheck.h"

#undef malloc
#undef calloc
#undef realloc
#undef free

static size_t live_allocations;
static size_t live_bytes;

static char *copy_source_file(const char *source_file)
{
    size_t length;
    char *copy;
    if (!source_file) source_file = "generated module";
    length = strlen(source_file);
    if (length == SIZE_MAX) return NULL;
    copy = (char *)malloc(length + 1);
    if (copy) memcpy(copy, source_file, length + 1);
    return copy;
}

static size_t allocation_size(const void *pointer)
{
    const stb_leakcheck_malloc_info *info;
    if (!pointer) return 0;
    info = (const stb_leakcheck_malloc_info *)pointer - 1;
    return info->size;
}

void *morph_runtime_leakcheck_malloc(
    size_t size,
    const char *source_file,
    int source_line)
{
    char *owned_source_file;
    void *pointer;
    if (size > INT_MAX ||
        size > SIZE_MAX - sizeof(stb_leakcheck_malloc_info) ||
        live_bytes > SIZE_MAX - size) {
        return NULL;
    }
    owned_source_file = copy_source_file(source_file);
    if (!owned_source_file) return NULL;
    pointer = stb_leakcheck_malloc(size, owned_source_file, source_line);
    if (pointer) {
        ++live_allocations;
        live_bytes += size;
    } else {
        free(owned_source_file);
    }
    return pointer;
}

void *morph_runtime_leakcheck_calloc(
    size_t count,
    size_t size,
    const char *source_file,
    int source_line)
{
    size_t total;
    void *pointer;
    if (size && count > SIZE_MAX / size) return NULL;
    total = count * size;
    pointer = morph_runtime_leakcheck_malloc(total, source_file, source_line);
    if (pointer) memset(pointer, 0, total);
    return pointer;
}

void *morph_runtime_leakcheck_realloc(
    void *pointer,
    size_t size,
    const char *source_file,
    int source_line)
{
    size_t previous_size;
    void *replacement;
    if (!pointer) {
        return morph_runtime_leakcheck_malloc(size, source_file, source_line);
    }
    if (!size) {
        morph_runtime_leakcheck_free(pointer);
        return NULL;
    }
    if (size > INT_MAX || size > SIZE_MAX - sizeof(stb_leakcheck_malloc_info)) {
        return NULL;
    }
    previous_size = allocation_size(pointer);
    if (size <= previous_size) return pointer;
    replacement = morph_runtime_leakcheck_malloc(
        size, source_file, source_line);
    if (!replacement) return NULL;
    memcpy(replacement, pointer, previous_size);
    morph_runtime_leakcheck_free(pointer);
    return replacement;
}

void morph_runtime_leakcheck_free(void *pointer)
{
    char *owned_source_file;
    stb_leakcheck_malloc_info *info;
    size_t size;
    if (!pointer) return;
    info = (stb_leakcheck_malloc_info *)pointer - 1;
    size = info->size;
    owned_source_file = (char *)info->file;
    if (live_allocations) --live_allocations;
    if (size <= live_bytes) live_bytes -= size;
    else live_bytes = 0;
    stb_leakcheck_free(pointer);
    free(owned_source_file);
}

size_t morph_runtime_leakcheck_live_allocations(void)
{
    return live_allocations;
}

size_t morph_runtime_leakcheck_live_bytes(void)
{
    return live_bytes;
}

size_t morph_runtime_leakcheck_report(void)
{
    if (live_allocations) {
        fprintf(stderr,
            "Morpheus generated-module leaks: %zu allocation(s), %zu byte(s)\n",
            live_allocations,
            live_bytes);
        stb_leakcheck_dumpmem();
    }
    return live_allocations;
}
