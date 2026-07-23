#ifndef MORPHEUS_NUKLEAR_FRAME_CACHE_H_
#define MORPHEUS_NUKLEAR_FRAME_CACHE_H_

#include <stdlib.h>
#include <string.h>

#ifndef NK_ZERO_COMMAND_MEMORY
#error "Morpheus frame caching requires NK_ZERO_COMMAND_MEMORY"
#endif

struct morph_nuklear_frame_cache {
    void *commands;
    nk_size size;
    nk_size capacity;
    int valid;
};

static int
morph_nuklear_frame_cacheable(const struct nk_context *ctx)
{
    const struct nk_command *command;
    if (!ctx) return 0;
    nk_foreach(command, (struct nk_context *)ctx) {
        /* Custom callbacks can depend on data behind an unchanged pointer. */
        if (command->type == NK_COMMAND_CUSTOM) return 0;
    }
    return 1;
}

static int
morph_nuklear_frame_matches(const struct morph_nuklear_frame_cache *cache,
    const struct nk_context *ctx)
{
    const void *commands;
    nk_size size;
    if (!cache || !cache->valid || !morph_nuklear_frame_cacheable(ctx)) return 0;
    size = ctx->memory.allocated;
    if (cache->size != size) return 0;
    if (!size) return 1;
    commands = nk_buffer_memory_const(&ctx->memory);
    return commands && memcmp(cache->commands, commands, size) == 0;
}

static int
morph_nuklear_frame_store(struct morph_nuklear_frame_cache *cache,
    const struct nk_context *ctx)
{
    const void *commands;
    nk_size size;
    if (!cache || !ctx) return 0;
    size = ctx->memory.allocated;
    commands = nk_buffer_memory_const(&ctx->memory);
    if (size > cache->capacity) {
        void *replacement;
        nk_size capacity = cache->capacity ? cache->capacity : 4096u;
        while (capacity < size) {
            if (capacity > size / 2u) {
                capacity = size;
                break;
            }
            capacity *= 2u;
        }
        replacement = realloc(cache->commands, capacity);
        if (!replacement) {
            cache->valid = 0;
            return 0;
        }
        cache->commands = replacement;
        cache->capacity = capacity;
    }
    if (size) {
        if (!commands) {
            cache->valid = 0;
            return 0;
        }
        memcpy(cache->commands, commands, size);
    }
    cache->size = size;
    cache->valid = 1;
    return 1;
}

static void
morph_nuklear_frame_cache_free(struct morph_nuklear_frame_cache *cache)
{
    if (!cache) return;
    free(cache->commands);
    memset(cache, 0, sizeof(*cache));
}

#endif
