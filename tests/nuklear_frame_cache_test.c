#include <stdio.h>

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_ZERO_COMMAND_MEMORY
#define NK_IMPLEMENTATION
#include "nuklear.h"

#include "morpheus_nuklear_frame_cache.h"

static float test_font_width(
    nk_handle handle, float height, const char *text, int length)
{
    (void)handle;
    (void)height;
    (void)text;
    return (float)length * 8.0f;
}

static void test_custom_draw(void *canvas, short x, short y,
    unsigned short width, unsigned short height, nk_handle data)
{
    (void)canvas;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)data;
}

static void build_frame(struct nk_context *ctx, const char *label)
{
    if (nk_begin(ctx, "cache-test", nk_rect(0, 0, 320, 200), 0)) {
        nk_layout_row_dynamic(ctx, 24.0f, 1);
        nk_label(ctx, label, NK_TEXT_LEFT);
    }
    nk_end(ctx);
}

static void build_custom_frame(struct nk_context *ctx)
{
    if (nk_begin(ctx, "cache-test", nk_rect(0, 0, 320, 200), 0)) {
        struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
        nk_push_custom(canvas, nk_rect(10, 10, 20, 20),
            test_custom_draw, nk_handle_ptr(ctx));
    }
    nk_end(ctx);
}

int main(void)
{
    struct nk_context ctx;
    struct nk_user_font font = {0};
    struct morph_nuklear_frame_cache cache = {0};

    font.height = 15.0f;
    font.width = test_font_width;
    if (!nk_init_default(&ctx, &font)) return 1;

    build_frame(&ctx, "unchanged");
    if (morph_nuklear_frame_matches(&cache, &ctx)) return 2;
    if (!morph_nuklear_frame_store(&cache, &ctx)) return 3;
    if (!morph_nuklear_frame_matches(&cache, &ctx)) return 4;

    nk_clear(&ctx);
    build_frame(&ctx, "unchanged");
    if (!morph_nuklear_frame_matches(&cache, &ctx)) return 5;

    nk_clear(&ctx);
    build_frame(&ctx, "changed");
    if (morph_nuklear_frame_matches(&cache, &ctx)) return 6;
    if (!morph_nuklear_frame_store(&cache, &ctx)) return 7;

    nk_clear(&ctx);
    build_custom_frame(&ctx);
    if (!morph_nuklear_frame_store(&cache, &ctx)) return 8;
    if (morph_nuklear_frame_matches(&cache, &ctx)) return 9;

    nk_clear(&ctx);
    if (!morph_nuklear_frame_store(&cache, &ctx)) return 10;
    if (!morph_nuklear_frame_matches(&cache, &ctx)) return 11;

    morph_nuklear_frame_cache_free(&cache);
    nk_free(&ctx);
    puts("PASS: unchanged Nuklear frames match and mutable custom frames do not");
    return 0;
}
