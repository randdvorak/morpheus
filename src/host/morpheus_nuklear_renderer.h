#ifndef MORPHEUS_NUKLEAR_RENDERER_H_
#define MORPHEUS_NUKLEAR_RENDERER_H_

#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>

#include "morpheus_nuklear_metal.h"
#include "morpheus_nuklear_quartz.h"

typedef enum morph_nuklear_renderer_kind {
    MORPH_NUKLEAR_RENDERER_METAL = 0,
    MORPH_NUKLEAR_RENDERER_QUARTZ = 1
} morph_nuklear_renderer_kind;

struct morph_nuklear_renderer {
    morph_nuklear_renderer_kind kind;
    SDL_Window *window;
    SDL_MetalView metal_view;
    CAMetalLayer *metal_layer;
    struct nk_metal metal;
    struct nk_quartz quartz;
};

static morph_nuklear_renderer_kind
morph_nuklear_renderer_kind_from_environment(void)
{
    const char *value = getenv("MORPHEUS_RENDERER");
    return value && strcmp(value, "quartz") == 0
        ? MORPH_NUKLEAR_RENDERER_QUARTZ
        : MORPH_NUKLEAR_RENDERER_METAL;
}

static SDL_WindowFlags
morph_nuklear_renderer_window_flags(morph_nuklear_renderer_kind kind)
{
    return kind == MORPH_NUKLEAR_RENDERER_METAL ? SDL_WINDOW_METAL : 0;
}

static int
morph_nuklear_renderer_init(struct morph_nuklear_renderer *renderer,
    SDL_Window *window, morph_nuklear_renderer_kind kind)
{
    if (!renderer || !window) return 0;
    *renderer = (struct morph_nuklear_renderer){0};
    renderer->kind = kind;
    renderer->window = window;
    if (kind == MORPH_NUKLEAR_RENDERER_QUARTZ) {
        return nk_quartz_init(&renderer->quartz, window);
    }
    renderer->metal_view = SDL_Metal_CreateView(window);
    if (!renderer->metal_view) return 0;
    renderer->metal_layer = (__bridge CAMetalLayer *)
        SDL_Metal_GetLayer(renderer->metal_view);
    if (!renderer->metal_layer ||
        !nk_metal_init(&renderer->metal, MTLPixelFormatBGRA8Unorm)) {
        SDL_Metal_DestroyView(renderer->metal_view);
        renderer->metal_view = NULL;
        return 0;
    }
    renderer->metal_layer.device = renderer->metal.device;
    renderer->metal_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    renderer->metal_layer.framebufferOnly = YES;
    renderer->metal_layer.displaySyncEnabled = YES;
    return 1;
}

static void
morph_nuklear_renderer_upload_atlas(struct morph_nuklear_renderer *renderer,
    const void *pixels, int width, int height, struct nk_font_atlas *atlas,
    struct nk_font *font)
{
    if (renderer->kind == MORPH_NUKLEAR_RENDERER_QUARTZ) {
        nk_quartz_upload_atlas(&renderer->quartz, atlas, font);
    } else {
        nk_metal_upload_atlas(&renderer->metal,
            pixels, width, height, atlas);
    }
}

static void
morph_nuklear_renderer_invalidate(struct morph_nuklear_renderer *renderer)
{
    if (!renderer) return;
    if (renderer->kind == MORPH_NUKLEAR_RENDERER_QUARTZ) {
        nk_quartz_invalidate(&renderer->quartz);
    } else {
        nk_metal_invalidate(&renderer->metal);
    }
}

static unsigned long long
morph_nuklear_renderer_attempted_frame_count(
    const struct morph_nuklear_renderer *renderer)
{
    if (!renderer) return 0;
    return renderer->kind == MORPH_NUKLEAR_RENDERER_QUARTZ
        ? renderer->quartz.attempted_frame_count
        : renderer->metal.attempted_frame_count;
}

static void *
morph_nuklear_renderer_metal_device(
    const struct morph_nuklear_renderer *renderer)
{
    if (!renderer || renderer->kind != MORPH_NUKLEAR_RENDERER_METAL) return NULL;
    return (__bridge void *)renderer->metal.device;
}

static void
morph_nuklear_renderer_render(struct morph_nuklear_renderer *renderer,
    struct nk_context *ctx, struct nk_colorf clear, enum nk_anti_aliasing aa)
{
    int pixel_width, pixel_height, window_width, window_height;
    if (!renderer || !ctx || !renderer->window) return;
    SDL_GetWindowSize(renderer->window, &window_width, &window_height);
    SDL_GetWindowSizeInPixels(renderer->window, &pixel_width, &pixel_height);
    if (window_width <= 0 || window_height <= 0 ||
        pixel_width <= 0 || pixel_height <= 0) {
        nk_clear(ctx);
        return;
    }
    if (renderer->kind == MORPH_NUKLEAR_RENDERER_QUARTZ) {
        nk_quartz_render(&renderer->quartz, ctx, clear, aa);
    } else {
        renderer->metal_layer.drawableSize = CGSizeMake(pixel_width, pixel_height);
        nk_metal_render(&renderer->metal, ctx,
            renderer->metal_layer, clear, aa);
    }
}

static void
morph_nuklear_renderer_shutdown(struct morph_nuklear_renderer *renderer)
{
    if (!renderer) return;
    if (renderer->kind == MORPH_NUKLEAR_RENDERER_QUARTZ) {
        nk_quartz_shutdown(&renderer->quartz);
    } else {
        nk_metal_shutdown(&renderer->metal);
        if (renderer->metal_view) SDL_Metal_DestroyView(renderer->metal_view);
        renderer->metal_view = NULL;
        renderer->metal_layer = nil;
    }
}

#endif
