#include <stdio.h>
#include <stdlib.h>

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_UINT_DRAW_INDEX
#define NK_IMPLEMENTATION
#include "nuklear.h"

struct test_vertex {
    float position[2];
    float uv[2];
    nk_byte color[4];
};

static float test_font_width(
    nk_handle handle, float height, const char *text, int length)
{
    (void)handle;
    (void)height;
    (void)text;
    return (float)length * 8.0f;
}

static void test_font_glyph(
    nk_handle handle,
    float height,
    struct nk_user_font_glyph *glyph,
    nk_rune codepoint,
    nk_rune next_codepoint)
{
    (void)handle;
    (void)codepoint;
    (void)next_codepoint;
    glyph->uv[0] = nk_vec2(0, 0);
    glyph->uv[1] = nk_vec2(1, 1);
    glyph->offset = nk_vec2(0, 0);
    glyph->width = height;
    glyph->height = height;
    glyph->xadvance = height;
}

int main(void)
{
    struct nk_context ctx;
    struct nk_user_font font = {0};
    struct nk_buffer commands;
    struct nk_buffer vertices;
    struct nk_buffer indices;
    struct nk_convert_config config;
    struct nk_command_buffer *canvas;
    void *vertex_memory;
    void *index_memory;
    nk_size vertex_bytes;
    nk_size index_bytes;
    nk_flags result;
    int index;
    unsigned char small_vertex_buffer[256];
    unsigned char small_index_buffer[256];
    static const struct nk_draw_vertex_layout_element layout[] = {
        {NK_VERTEX_POSITION, NK_FORMAT_FLOAT,
            NK_OFFSETOF(struct test_vertex, position)},
        {NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT,
            NK_OFFSETOF(struct test_vertex, uv)},
        {NK_VERTEX_COLOR, NK_FORMAT_R8G8B8A8,
            NK_OFFSETOF(struct test_vertex, color)},
        {NK_VERTEX_LAYOUT_END}
    };

    font.height = 15.0f;
    font.width = test_font_width;
    font.query = test_font_glyph;
    if (sizeof(nk_draw_index) != 4 || !nk_init_default(&ctx, &font)) return 1;
    if (!nk_begin(&ctx, "large-draw", nk_rect(0, 0, 800, 600), 0)) return 2;
    canvas = nk_window_get_canvas(&ctx);
    for (index = 0; index < 17000; ++index) {
        nk_fill_rect(canvas,
            nk_rect((float)(index % 800), (float)(index % 600), 1, 1),
            0,
            nk_rgb(255, 255, 255));
    }
    nk_end(&ctx);

    nk_buffer_init_default(&commands);
    nk_buffer_init_fixed(&vertices,
        small_vertex_buffer, sizeof(small_vertex_buffer));
    nk_buffer_init_fixed(&indices,
        small_index_buffer, sizeof(small_index_buffer));
    nk_zero(&config, sizeof(config));
    config.vertex_layout = layout;
    config.vertex_size = sizeof(struct test_vertex);
    config.vertex_alignment = NK_ALIGNOF(struct test_vertex);
    config.circle_segment_count = 22;
    config.curve_segment_count = 22;
    config.arc_segment_count = 22;
    config.global_alpha = 1.0f;
    config.shape_AA = NK_ANTI_ALIASING_OFF;
    config.line_AA = NK_ANTI_ALIASING_OFF;

    result = nk_convert(&ctx, &commands, &vertices, &indices, &config);
    if (!(result & NK_CONVERT_VERTEX_BUFFER_FULL) ||
        !(result & NK_CONVERT_ELEMENT_BUFFER_FULL)) {
        fprintf(stderr, "small fixed buffers did not report overflow: flags=%u\n",
            (unsigned int)result);
        return 3;
    }
    vertex_bytes = vertices.needed;
    index_bytes = indices.needed;
    vertex_memory = malloc(vertex_bytes);
    index_memory = malloc(index_bytes);
    if (!vertex_memory || !index_memory) return 4;

    nk_buffer_clear(&commands);
    nk_buffer_init_fixed(&vertices, vertex_memory, vertex_bytes);
    nk_buffer_init_fixed(&indices, index_memory, index_bytes);
    result = nk_convert(&ctx, &commands, &vertices, &indices, &config);
    if (result != NK_CONVERT_SUCCESS || ctx.draw_list.vertex_count <= NK_USHORT_MAX) {
        fprintf(stderr, "fixed-buffer retry failed: flags=%u vertices=%u\n",
            (unsigned int)result,
            ctx.draw_list.vertex_count);
        return 5;
    }

    free(index_memory);
    free(vertex_memory);
    nk_buffer_free(&commands);
    nk_free(&ctx);
    puts("PASS: Nuklear grew fixed buffers and converted more than 65,535 vertices");
    return 0;
}
