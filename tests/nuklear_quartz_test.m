#include <stdio.h>
#include <stdlib.h>

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_ZERO_COMMAND_MEMORY
#define NK_IMPLEMENTATION
#include "nuklear.h"

#define NK_QUARTZ_IMPLEMENTATION
#include "morpheus_nuklear_quartz.h"

int main(void)
{
    enum { width = 160, height = 120 };
    struct nk_context ctx;
    struct nk_font_atlas atlas;
    struct nk_font *font;
    struct nk_quartz quartz = {0};
    struct nk_colorf clear = {0, 0, 0, 1};
    struct nk_command_buffer *canvas;
    CGColorSpaceRef color_space;
    CGContextRef bitmap;
    CGDataProviderRef image_provider;
    CGImageRef image;
    struct nk_image nuklear_image;
    unsigned char *pixels;
    static const unsigned char image_pixels[] = {
        0, 255, 0, 255, 0, 255, 0, 255,
        0, 255, 0, 255, 0, 255, 0, 255
    };
    const void *atlas_pixels;
    int atlas_width, atlas_height;
    int red_pixels = 0;
    int blue_pixels = 0;
    int green_pixels = 0;
    int white_pixels = 0;
    int index;

    if (!nk_init_default(&ctx, NULL)) return 1;
    nk_font_atlas_init_default(&atlas);
    nk_font_atlas_begin(&atlas);
    font = nk_font_atlas_add_default(&atlas, 15.0f, NULL);
    atlas_pixels = nk_font_atlas_bake(
        &atlas, &atlas_width, &atlas_height, NK_FONT_ATLAS_RGBA32);
    if (!font || !atlas_pixels || atlas_width <= 0 || atlas_height <= 0) return 2;
    nk_quartz_upload_atlas(&quartz, &atlas, font);
    if (!quartz.font || !quartz.text_font || !quartz.has_font_info ||
        !stbtt_FindGlyphIndex(&quartz.font_info, 'Q')) {
        fprintf(stderr,
            "Quartz font did not map the test glyph: cg=%d ct=%d stb=%d glyph=%d\n",
            quartz.font != NULL, quartz.text_font != NULL,
            quartz.has_font_info,
            quartz.has_font_info
                ? stbtt_FindGlyphIndex(&quartz.font_info, 'Q') : 0);
        return 6;
    }
    nk_style_set_font(&ctx, &font->handle);

    pixels = calloc((size_t)width * height, 4u);
    color_space = CGColorSpaceCreateDeviceRGB();
    bitmap = CGBitmapContextCreate(pixels, width, height, 8, width * 4,
        color_space, kCGBitmapByteOrder32Big | kCGImageAlphaPremultipliedLast);
    image_provider = CGDataProviderCreateWithData(
        NULL, image_pixels, sizeof(image_pixels), NULL);
    image = image_provider ? CGImageCreate(2, 2, 8, 32, 8, color_space,
        kCGBitmapByteOrder32Big | kCGImageAlphaLast, image_provider,
        NULL, false, kCGRenderingIntentDefault) : NULL;
    CGColorSpaceRelease(color_space);
    if (image_provider) CGDataProviderRelease(image_provider);
    if (!pixels || !bitmap || !image) return 3;
    nuklear_image = nk_image_ptr(image);

    if (nk_begin(&ctx, "Quartz", nk_rect(0, 0, width, height),
            NK_WINDOW_NO_SCROLLBAR)) {
        canvas = nk_window_get_canvas(&ctx);
        nk_fill_rect(canvas, nk_rect(12, 12, 40, 30), 0,
            nk_rgba(255, 0, 0, 255));
        nk_push_scissor(canvas, nk_rect(70, 10, 20, 20));
        nk_fill_rect(canvas, nk_rect(65, 5, 40, 40), 0,
            nk_rgba(0, 0, 255, 255));
        nk_push_scissor(canvas, nk_rect(0, 0, width, height));
        nk_draw_image(canvas, nk_rect(110, 10, 30, 30),
            &nuklear_image, nk_rgba(255, 255, 255, 255));
        nk_draw_text(canvas, nk_rect(12, 55, 100, 20),
            "Quartz", 6, &font->handle,
            nk_rgba(0, 0, 0, 0), nk_rgba(255, 255, 255, 255));
    }
    nk_end(&ctx);
    nk_quartz_draw_commands(&quartz, bitmap, &ctx,
        CGSizeMake(width, height), clear,
        NK_ANTI_ALIASING_ON);

    for (index = 0; index < width * height; ++index) {
        const unsigned char *pixel = &pixels[index * 4];
        if (pixel[0] > 200 && pixel[1] < 40 && pixel[2] < 40) red_pixels++;
        if (pixel[0] < 40 && pixel[1] < 40 && pixel[2] > 200) blue_pixels++;
        if (pixel[0] < 40 && pixel[1] > 200 && pixel[2] < 40) green_pixels++;
        if (pixel[0] > 200 && pixel[1] > 200 && pixel[2] > 200) white_pixels++;
    }
    CGImageRelease(image);
    CGContextRelease(bitmap);
    free(pixels);
    nk_clear(&ctx);
    nk_quartz_shutdown(&quartz);
    nk_font_atlas_clear(&atlas);
    nk_free(&ctx);
    if (red_pixels < 500 || blue_pixels < 300 || blue_pixels > 500 ||
        green_pixels < 700 || green_pixels > 1000) {
        fprintf(stderr,
            "unexpected Quartz coverage: red=%d blue=%d green=%d white=%d\n",
            red_pixels, blue_pixels, green_pixels, white_pixels);
        return 4;
    }
    if (white_pixels < 10) {
        fprintf(stderr, "Quartz text did not render: white=%d\n", white_pixels);
        return 5;
    }
    puts("PASS: Quartz renders Nuklear primitives, clipping, and text");
    return 0;
}
