#ifndef MORPHEUS_APP_API_H
#define MORPHEUS_APP_API_H

#include <string.h>

#include "morpheus/sdk.h"

#define MORPHEUS_HOST_ABI_VERSION 4u
#define MORPHEUS_APP_ABI_VERSION 3u

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_INCLUDE_COMMAND_USERDATA
/* Generated apps may emit more than 65,535 vertices in a single frame. */
#define NK_UINT_DRAW_INDEX
/* main.m owns the one NK_IMPLEMENTATION translation unit. */
#ifndef NK_NUKLEAR_H_
#include "nuklear.h"
#endif

typedef enum morph_render_mode {
    MORPHEUS_RENDER_EMBEDDED = 0,
    MORPHEUS_RENDER_NUKLEAR_WINDOWS = 1
} morph_render_mode;

typedef struct morph_capability {
    const char *identifier;
    unsigned int abi_version;
    unsigned long api_size;
    const void *api;
    void *context;
} morph_capability;

typedef struct morph_capability_registry {
    const morph_capability *entries;
    unsigned long count;
} morph_capability_registry;

typedef struct morph_host morph_host;

struct morph_host {
    unsigned int abi_version;
    void *user_data;
    void (*log)(morph_host *host, const char *message);
    void (*ui_label)(morph_host *host, const char *text);
    int (*ui_button)(morph_host *host, const char *text);
    struct nk_context *nuklear;
    morph_http_service *http;
    morph_image_service *images;
    const morph_capability_registry *capabilities;
};

/* Return the newest compatible provider, or NULL when it is unavailable. */
static inline const morph_capability *morph_host_find_capability(
    const morph_host *host,
    const char *identifier,
    unsigned int minimum_abi_version)
{
    const morph_capability *best = NULL;
    unsigned long index;
    if (!host || !host->capabilities || !host->capabilities->entries ||
        !identifier) return NULL;
    for (index = 0; index < host->capabilities->count; ++index) {
        const morph_capability *candidate = &host->capabilities->entries[index];
        if (!candidate->identifier || !candidate->api ||
            candidate->abi_version < minimum_abi_version ||
            strcmp(candidate->identifier, identifier) != 0) continue;
        if (!best || candidate->abi_version > best->abi_version) best = candidate;
    }
    return best;
}

typedef struct morph_app_api {
    unsigned int abi_version;
    const char *name;
    int (*create)(morph_host *host, void **state);
    void (*destroy)(morph_host *host, void *state);
    void (*update)(morph_host *host, void *state, double dt);
    void (*render_ui)(morph_host *host, void *state);
    int (*save_state)(
        morph_host *host,
        void *state,
        const void **data,
        unsigned long *size);
    int (*load_state)(
        morph_host *host,
        void **state,
        const void *data,
        unsigned long size);
} morph_app_api;

typedef const morph_app_api *(*morph_app_entry_fn)(void);
typedef unsigned int (*morph_app_render_mode_fn)(void);

#ifdef MORPHEUS_ENABLE_RUNTIME_LEAKCHECK
#include "morpheus/leakcheck.h"
#endif

#endif
