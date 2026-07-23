/* Morpheus Nuklear renderer for Metal. Public domain. */
#ifndef MORPHEUS_NUKLEAR_METAL_H_
#define MORPHEUS_NUKLEAR_METAL_H_

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "morpheus_nuklear_frame_cache.h"

#define NK_METAL_FRAMES_IN_FLIGHT 3u
#define NK_METAL_INITIAL_VERTEX_CAPACITY (256u * 1024u)
#define NK_METAL_INITIAL_INDEX_CAPACITY (128u * 1024u)
#define NK_METAL_MAX_BUFFER_CAPACITY (64u * 1024u * 1024u)

struct nk_metal_vertex {
    float position[2];
    float uv[2];
    nk_byte color[4];
};

struct nk_metal_frame {
    id<MTLBuffer> vertex_buffer;
    id<MTLBuffer> index_buffer;
    id<MTLCommandBuffer> command_buffer;
    nk_size vertex_capacity;
    nk_size index_capacity;
};

struct nk_metal {
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    id<MTLRenderPipelineState> pipeline;
    id<MTLSamplerState> sampler;
    id<MTLTexture> font_texture;
    struct nk_draw_null_texture null;
    struct nk_buffer commands;
    struct nk_metal_frame frames[NK_METAL_FRAMES_IN_FLIGHT];
    unsigned int frame_index;
    struct morph_nuklear_frame_cache frame_cache;
    CGSize last_drawable_size;
    CGSize last_viewport_size;
    struct nk_colorf last_clear;
    enum nk_anti_aliasing last_aa;
    int has_presentation_state;
    int invalidated;
    int last_frame_rendered;
    nk_size last_vertex_bytes;
    nk_size last_index_bytes;
    unsigned int last_draw_count;
    unsigned long long rendered_frame_count;
    unsigned long long skipped_frame_count;
    unsigned long long attempted_frame_count;
};

NK_API int nk_metal_init(struct nk_metal *metal, MTLPixelFormat format);
NK_API void nk_metal_upload_atlas(struct nk_metal *metal, const void *pixels,
    int width, int height, struct nk_font_atlas *atlas);
NK_API void nk_metal_invalidate(struct nk_metal *metal);
NK_API void nk_metal_render(struct nk_metal *metal, struct nk_context *ctx,
    CAMetalLayer *layer, struct nk_colorf clear, enum nk_anti_aliasing aa);
NK_API void nk_metal_shutdown(struct nk_metal *metal);

#ifdef NK_METAL_IMPLEMENTATION

NK_INTERN nk_size
nk_metal_next_capacity(nk_size current, nk_size needed)
{
    nk_size capacity = current ? current : 1u;
    while (capacity < needed && capacity < NK_METAL_MAX_BUFFER_CAPACITY) {
        if (capacity > NK_METAL_MAX_BUFFER_CAPACITY / 2u) {
            capacity = NK_METAL_MAX_BUFFER_CAPACITY;
        } else {
            capacity *= 2u;
        }
    }
    return capacity >= needed ? capacity : 0;
}

NK_INTERN int
nk_metal_resize_vertex_buffer(struct nk_metal *metal,
    struct nk_metal_frame *frame, nk_size needed)
{
    nk_size requested;
    id<MTLBuffer> replacement;
    if (frame->vertex_buffer && frame->vertex_capacity >= needed) return 1;
    requested = nk_metal_next_capacity(
        frame->vertex_capacity ? frame->vertex_capacity :
            NK_METAL_INITIAL_VERTEX_CAPACITY, needed);
    if (!requested) return 0;
    replacement = [metal->device newBufferWithLength:(NSUInteger)requested
        options:MTLResourceStorageModeShared];
    if (!replacement) return 0;
    frame->vertex_buffer = replacement;
    frame->vertex_capacity = requested;
    return 1;
}

NK_INTERN int
nk_metal_resize_index_buffer(struct nk_metal *metal,
    struct nk_metal_frame *frame, nk_size needed)
{
    nk_size requested;
    id<MTLBuffer> replacement;
    if (frame->index_buffer && frame->index_capacity >= needed) return 1;
    requested = nk_metal_next_capacity(
        frame->index_capacity ? frame->index_capacity :
            NK_METAL_INITIAL_INDEX_CAPACITY, needed);
    if (!requested) return 0;
    replacement = [metal->device newBufferWithLength:(NSUInteger)requested
        options:MTLResourceStorageModeShared];
    if (!replacement) return 0;
    frame->index_buffer = replacement;
    frame->index_capacity = requested;
    return 1;
}

NK_INTERN int
nk_metal_prepare_frame(struct nk_metal *metal, struct nk_metal_frame *frame,
    struct nk_context *ctx, const struct nk_convert_config *config)
{
    struct nk_buffer vertices;
    struct nk_buffer indices;
    nk_flags result;
    unsigned int attempt;

    if (!nk_metal_resize_vertex_buffer(metal, frame,
            NK_METAL_INITIAL_VERTEX_CAPACITY) ||
        !nk_metal_resize_index_buffer(metal, frame,
            NK_METAL_INITIAL_INDEX_CAPACITY)) return 0;

    for (attempt = 0; attempt < 4u; ++attempt) {
        nk_buffer_clear(&metal->commands);
        nk_buffer_init_fixed(&vertices,
            frame->vertex_buffer.contents, frame->vertex_capacity);
        nk_buffer_init_fixed(&indices,
            frame->index_buffer.contents, frame->index_capacity);
        result = nk_convert(ctx, &metal->commands, &vertices, &indices, config);
        if (result == NK_CONVERT_SUCCESS) {
            metal->last_vertex_bytes = vertices.allocated;
            metal->last_index_bytes = indices.allocated;
            return 1;
        }
        if (result & NK_CONVERT_COMMAND_BUFFER_FULL) return 0;
        if ((result & NK_CONVERT_VERTEX_BUFFER_FULL) &&
            !nk_metal_resize_vertex_buffer(metal, frame,
                vertices.needed)) return 0;
        if ((result & NK_CONVERT_ELEMENT_BUFFER_FULL) &&
            !nk_metal_resize_index_buffer(metal, frame,
                indices.needed)) return 0;
        if (!(result & (NK_CONVERT_VERTEX_BUFFER_FULL |
                        NK_CONVERT_ELEMENT_BUFFER_FULL))) return 0;
    }
    return 0;
}

NK_API int
nk_metal_init(struct nk_metal *metal, MTLPixelFormat format)
{
    NSError *error = nil;
    id<MTLLibrary> library;
    id<MTLFunction> vertex_function;
    id<MTLFunction> fragment_function;
    MTLRenderPipelineDescriptor *pipeline;
    MTLSamplerDescriptor *sampler;
    static NSString *source =
        @"#include <metal_stdlib>\n"
         "using namespace metal;\n"
         "struct V { packed_float2 pos; packed_float2 uv; uchar4 color; };\n"
         "struct O { float4 pos [[position]]; float2 uv; float4 color; };\n"
         "vertex O nk_vertex(const device V *v [[buffer(0)]],\n"
         "                   constant float2 &viewport [[buffer(1)]],\n"
         "                   uint id [[vertex_id]]) {\n"
         "  O o; o.pos=float4(v[id].pos.x*2.0/viewport.x-1.0,\n"
         "                  1.0-v[id].pos.y*2.0/viewport.y,0,1);\n"
         "  o.uv=v[id].uv; o.color=float4(v[id].color)/255.0; return o;\n"
         "}\n"
         "fragment float4 nk_fragment(O in [[stage_in]],\n"
         "  texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]]) {\n"
         "  return in.color * tex.sample(smp, in.uv);\n"
         "}\n";

    NK_MEMSET(metal, 0, sizeof(*metal));
    metal->invalidated = 1;
    metal->device = MTLCreateSystemDefaultDevice();
    if (!metal->device) return 0;
    metal->queue = [metal->device newCommandQueue];
    library = [metal->device newLibraryWithSource:source options:nil error:&error];
    if (!library) {
        NSLog(@"Nuklear Metal shader error: %@", error);
        return 0;
    }
    vertex_function = [library newFunctionWithName:@"nk_vertex"];
    fragment_function = [library newFunctionWithName:@"nk_fragment"];
    pipeline = [MTLRenderPipelineDescriptor new];
    pipeline.vertexFunction = vertex_function;
    pipeline.fragmentFunction = fragment_function;
    pipeline.colorAttachments[0].pixelFormat = format;
    pipeline.colorAttachments[0].blendingEnabled = YES;
    pipeline.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
    pipeline.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
    pipeline.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    pipeline.colorAttachments[0].destinationRGBBlendFactor =
        MTLBlendFactorOneMinusSourceAlpha;
    pipeline.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
    pipeline.colorAttachments[0].destinationAlphaBlendFactor =
        MTLBlendFactorOneMinusSourceAlpha;
    metal->pipeline = [metal->device
        newRenderPipelineStateWithDescriptor:pipeline error:&error];
    if (!metal->pipeline) {
        NSLog(@"Nuklear Metal pipeline error: %@", error);
        return 0;
    }
    sampler = [MTLSamplerDescriptor new];
    sampler.minFilter = MTLSamplerMinMagFilterLinear;
    sampler.magFilter = MTLSamplerMinMagFilterLinear;
    sampler.sAddressMode = MTLSamplerAddressModeClampToEdge;
    sampler.tAddressMode = MTLSamplerAddressModeClampToEdge;
    metal->sampler = [metal->device newSamplerStateWithDescriptor:sampler];
    nk_buffer_init_default(&metal->commands);
    return metal->queue != nil && metal->sampler != nil;
}

NK_API void
nk_metal_upload_atlas(struct nk_metal *metal, const void *pixels, int width,
    int height, struct nk_font_atlas *atlas)
{
    MTLTextureDescriptor *desc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
        width:(NSUInteger)width height:(NSUInteger)height mipmapped:NO];
    metal->font_texture = [metal->device newTextureWithDescriptor:desc];
    [metal->font_texture replaceRegion:MTLRegionMake2D(0, 0, width, height)
        mipmapLevel:0 withBytes:pixels bytesPerRow:(NSUInteger)width * 4];
    nk_font_atlas_end(atlas,
        nk_handle_ptr((__bridge void *)metal->font_texture), &metal->null);
}

NK_API void
nk_metal_invalidate(struct nk_metal *metal)
{
    if (metal) metal->invalidated = 1;
}

NK_INTERN int
nk_metal_presentation_changed(const struct nk_metal *metal,
    CGSize drawable_size, CGSize viewport_size, struct nk_colorf clear,
    enum nk_anti_aliasing aa)
{
    if (!metal->has_presentation_state) return 1;
    return metal->last_drawable_size.width != drawable_size.width ||
        metal->last_drawable_size.height != drawable_size.height ||
        metal->last_viewport_size.width != viewport_size.width ||
        metal->last_viewport_size.height != viewport_size.height ||
        metal->last_clear.r != clear.r || metal->last_clear.g != clear.g ||
        metal->last_clear.b != clear.b || metal->last_clear.a != clear.a ||
        metal->last_aa != aa;
}

NK_API void
nk_metal_render(struct nk_metal *metal, struct nk_context *ctx,
    CAMetalLayer *layer, struct nk_colorf clear, enum nk_anti_aliasing aa)
{
    struct nk_convert_config config;
    const struct nk_draw_command *cmd;
    struct nk_metal_frame *frame;
    id<CAMetalDrawable> drawable = nil;
    id<MTLCommandBuffer> command_buffer = nil;
    id<MTLRenderCommandEncoder> encoder = nil;
    id<MTLTexture> bound_texture = nil;
    MTLRenderPassDescriptor *pass;
    MTLScissorRect bound_scissor = {0, 0, 0, 0};
    CGSize drawable_size;
    CGSize viewport_size;
    float viewport[2];
    float scale_x, scale_y;
    nk_size index_offset = 0;
    int has_bound_scissor = 0;
    int force_redraw;
    static const struct nk_draw_vertex_layout_element layout[] = {
        {NK_VERTEX_POSITION, NK_FORMAT_FLOAT,
            NK_OFFSETOF(struct nk_metal_vertex, position)},
        {NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT,
            NK_OFFSETOF(struct nk_metal_vertex, uv)},
        {NK_VERTEX_COLOR, NK_FORMAT_R8G8B8A8,
            NK_OFFSETOF(struct nk_metal_vertex, color)},
        {NK_VERTEX_LAYOUT_END}
    };

    metal->last_vertex_bytes = 0;
    metal->last_index_bytes = 0;
    metal->last_draw_count = 0;
    metal->last_frame_rendered = 0;
    metal->attempted_frame_count++;
    drawable_size = layer.drawableSize;
    viewport_size = layer.bounds.size;
    force_redraw = metal->invalidated || nk_metal_presentation_changed(
        metal, drawable_size, viewport_size, clear, aa);
    if (!force_redraw && morph_nuklear_frame_matches(&metal->frame_cache, ctx)) {
        metal->skipped_frame_count++;
        goto cleanup;
    }

    frame = &metal->frames[metal->frame_index];
    metal->frame_index = (metal->frame_index + 1u) % NK_METAL_FRAMES_IN_FLIGHT;
    if (frame->command_buffer) {
        [frame->command_buffer waitUntilCompleted];
        frame->command_buffer = nil;
    }

    NK_MEMSET(&config, 0, sizeof(config));
    config.vertex_layout = layout;
    config.vertex_size = sizeof(struct nk_metal_vertex);
    config.vertex_alignment = NK_ALIGNOF(struct nk_metal_vertex);
    config.tex_null = metal->null;
    config.circle_segment_count = 22;
    config.curve_segment_count = 22;
    config.arc_segment_count = 22;
    config.global_alpha = 1.0f;
    config.shape_AA = aa;
    config.line_AA = aa;
    if (!nk_metal_prepare_frame(metal, frame, ctx, &config)) goto cleanup;

    drawable = [layer nextDrawable];
    if (!drawable) goto cleanup;
    viewport[0] = (float)viewport_size.width;
    viewport[1] = (float)viewport_size.height;
    if (viewport[0] <= 0.0f || viewport[1] <= 0.0f) goto cleanup;
    scale_x = (float)drawable_size.width / viewport[0];
    scale_y = (float)drawable_size.height / viewport[1];

    pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = drawable.texture;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor =
        MTLClearColorMake(clear.r, clear.g, clear.b, clear.a);
    command_buffer = [metal->queue commandBuffer];
    if (!command_buffer) goto cleanup;
    encoder = [command_buffer renderCommandEncoderWithDescriptor:pass];
    if (!encoder) goto cleanup;
    [encoder setRenderPipelineState:metal->pipeline];
    [encoder setVertexBuffer:frame->vertex_buffer offset:0 atIndex:0];
    [encoder setVertexBytes:viewport length:sizeof(viewport) atIndex:1];
    [encoder setFragmentSamplerState:metal->sampler atIndex:0];

    nk_draw_foreach(cmd, ctx, &metal->commands) {
        NSUInteger x, y, w, h;
        MTLScissorRect scissor;
        id<MTLTexture> texture;
        if (!cmd->elem_count) continue;
        x = (NSUInteger)NK_MAX(0.0f, cmd->clip_rect.x * scale_x);
        y = (NSUInteger)NK_MAX(0.0f, cmd->clip_rect.y * scale_y);
        w = (NSUInteger)NK_MAX(0.0f, cmd->clip_rect.w * scale_x);
        h = (NSUInteger)NK_MAX(0.0f, cmd->clip_rect.h * scale_y);
        if (x >= (NSUInteger)drawable_size.width ||
            y >= (NSUInteger)drawable_size.height) {
            index_offset += cmd->elem_count * sizeof(nk_draw_index);
            continue;
        }
        w = NK_MIN(w, (NSUInteger)drawable_size.width - x);
        h = NK_MIN(h, (NSUInteger)drawable_size.height - y);
        if (!w || !h) {
            index_offset += cmd->elem_count * sizeof(nk_draw_index);
            continue;
        }
        scissor = (MTLScissorRect){x, y, w, h};
        if (!has_bound_scissor ||
            scissor.x != bound_scissor.x || scissor.y != bound_scissor.y ||
            scissor.width != bound_scissor.width ||
            scissor.height != bound_scissor.height) {
            [encoder setScissorRect:scissor];
            bound_scissor = scissor;
            has_bound_scissor = 1;
        }
        texture = (__bridge id<MTLTexture>)cmd->texture.ptr;
        if (texture != bound_texture) {
            [encoder setFragmentTexture:texture atIndex:0];
            bound_texture = texture;
        }
        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
            indexCount:cmd->elem_count
            indexType:sizeof(nk_draw_index) == 2
                ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32
            indexBuffer:frame->index_buffer indexBufferOffset:index_offset];
        index_offset += cmd->elem_count * sizeof(nk_draw_index);
        metal->last_draw_count++;
    }
    [encoder endEncoding];
    encoder = nil;
    [command_buffer presentDrawable:drawable];
    [command_buffer commit];
    frame->command_buffer = command_buffer;
    metal->last_drawable_size = drawable_size;
    metal->last_viewport_size = viewport_size;
    metal->last_clear = clear;
    metal->last_aa = aa;
    metal->has_presentation_state = 1;
    metal->last_frame_rendered = 1;
    metal->rendered_frame_count++;
    metal->invalidated = !morph_nuklear_frame_store(&metal->frame_cache, ctx);

cleanup:
    if (encoder) [encoder endEncoding];
    nk_clear(ctx);
    nk_buffer_clear(&metal->commands);
}

NK_API void
nk_metal_shutdown(struct nk_metal *metal)
{
    unsigned int index;
    nk_buffer_free(&metal->commands);
    morph_nuklear_frame_cache_free(&metal->frame_cache);
    for (index = 0; index < NK_METAL_FRAMES_IN_FLIGHT; ++index) {
        if (metal->frames[index].command_buffer) {
            [metal->frames[index].command_buffer waitUntilCompleted];
            metal->frames[index].command_buffer = nil;
        }
        metal->frames[index].vertex_buffer = nil;
        metal->frames[index].index_buffer = nil;
        metal->frames[index].vertex_capacity = 0;
        metal->frames[index].index_capacity = 0;
    }
    metal->font_texture = nil;
    metal->sampler = nil;
    metal->pipeline = nil;
    metal->queue = nil;
    metal->device = nil;
}
#endif
#endif
