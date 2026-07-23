/* Morpheus Nuklear renderer for Quartz/Core Graphics. Public domain. */
#ifndef MORPHEUS_NUKLEAR_QUARTZ_H_
#define MORPHEUS_NUKLEAR_QUARTZ_H_

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreText/CoreText.h>

#include <SDL3/SDL.h>

#include "morpheus_nuklear_frame_cache.h"

struct nk_quartz {
    void *retained_view;
    struct nk_context *drawing_context;
    CGFontRef font;
    CTFontRef text_font;
    const struct nk_user_font *nuklear_font;
    stbtt_fontinfo font_info;
    CGGlyph fallback_glyph;
    int has_font_info;
    struct morph_nuklear_frame_cache frame_cache;
    CGSize last_viewport_size;
    struct nk_colorf clear;
    struct nk_colorf last_clear;
    enum nk_anti_aliasing aa;
    enum nk_anti_aliasing last_aa;
    int has_presentation_state;
    int invalidated;
    int did_draw;
    int last_frame_rendered;
    unsigned long long rendered_frame_count;
    unsigned long long skipped_frame_count;
    unsigned long long attempted_frame_count;
};

NK_API int nk_quartz_init(struct nk_quartz *quartz, SDL_Window *window);
NK_API void nk_quartz_upload_atlas(struct nk_quartz *quartz,
    struct nk_font_atlas *atlas, struct nk_font *font);
NK_API void nk_quartz_invalidate(struct nk_quartz *quartz);
NK_API void nk_quartz_render(struct nk_quartz *quartz, struct nk_context *ctx,
    struct nk_colorf clear, enum nk_anti_aliasing aa);
NK_API void nk_quartz_shutdown(struct nk_quartz *quartz);

#ifdef NK_QUARTZ_IMPLEMENTATION

@interface MorphNuklearQuartzView : NSView
@property(nonatomic, assign) struct nk_quartz *renderer;
@end

NK_INTERN void
nk_quartz_set_fill(CGContextRef context, struct nk_color color)
{
    CGContextSetRGBFillColor(context,
        (CGFloat)color.r / 255.0, (CGFloat)color.g / 255.0,
        (CGFloat)color.b / 255.0, (CGFloat)color.a / 255.0);
}

NK_INTERN void
nk_quartz_set_stroke(CGContextRef context, struct nk_color color,
    CGFloat thickness)
{
    CGContextSetRGBStrokeColor(context,
        (CGFloat)color.r / 255.0, (CGFloat)color.g / 255.0,
        (CGFloat)color.b / 255.0, (CGFloat)color.a / 255.0);
    CGContextSetLineWidth(context, thickness);
}

NK_INTERN void
nk_quartz_fill_multi_color(CGContextRef context,
    const struct nk_command_rect_multi_color *rect)
{
    enum { steps = 16 };
    int x, y;
    CGFloat cell_width = (CGFloat)rect->w / steps;
    CGFloat cell_height = (CGFloat)rect->h / steps;
    for (y = 0; y < steps; ++y) {
        CGFloat v = ((CGFloat)y + 0.5) / steps;
        for (x = 0; x < steps; ++x) {
            CGFloat u = ((CGFloat)x + 0.5) / steps;
            CGFloat w00 = (1.0 - u) * (1.0 - v);
            CGFloat w10 = u * (1.0 - v);
            CGFloat w11 = u * v;
            CGFloat w01 = (1.0 - u) * v;
            CGFloat red = (rect->left.r * w00 + rect->top.r * w10 +
                rect->right.r * w11 + rect->bottom.r * w01) / 255.0;
            CGFloat green = (rect->left.g * w00 + rect->top.g * w10 +
                rect->right.g * w11 + rect->bottom.g * w01) / 255.0;
            CGFloat blue = (rect->left.b * w00 + rect->top.b * w10 +
                rect->right.b * w11 + rect->bottom.b * w01) / 255.0;
            CGFloat alpha = (rect->left.a * w00 + rect->top.a * w10 +
                rect->right.a * w11 + rect->bottom.a * w01) / 255.0;
            CGRect cell = CGRectMake(
                rect->x + x * cell_width,
                rect->y + y * cell_height,
                cell_width + 0.5, cell_height + 0.5);
            CGContextSetRGBFillColor(context, red, green, blue, alpha);
            CGContextFillRect(context, cell);
        }
    }
}

NK_INTERN void
nk_quartz_draw_text(struct nk_quartz *quartz, CGContextRef context,
    const struct nk_command_text *text)
{
    int offset = 0;
    CGFloat x = text->x;
    CGFloat font_size = text->height;
    CGFloat base_size;
    CGFloat ascent;
    CGFloat glyph_scale;
    if (!quartz->font || !quartz->text_font || !text->font ||
        !text->font->query || text->length <= 0) return;
    base_size = CTFontGetSize(quartz->text_font);
    ascent = base_size > 0
        ? CTFontGetAscent(quartz->text_font) * font_size / base_size
        : font_size * 0.8;
    glyph_scale = base_size > 0 ? font_size / base_size : 1.0;
    if (glyph_scale <= 0) return;
    nk_quartz_set_fill(context, text->foreground);
    while (offset < text->length) {
        nk_rune rune = 0;
        nk_rune next = 0;
        int length = nk_utf_decode(text->string + offset, &rune,
            text->length - offset);
        struct nk_user_font_glyph glyph_info;
        CGGlyph glyph = 0;
        CGPoint position;
        if (length <= 0 || rune == NK_UTF_INVALID) break;
        if (offset + length < text->length) {
            (void)nk_utf_decode(text->string + offset + length, &next,
                text->length - offset - length);
        }
        NK_MEMSET(&glyph_info, 0, sizeof(glyph_info));
        text->font->query(text->font->userdata, text->height, &glyph_info,
            rune, next);
        if (quartz->has_font_info) {
            glyph = (CGGlyph)stbtt_FindGlyphIndex(
                &quartz->font_info, (int)rune);
            if (!glyph) glyph = quartz->fallback_glyph;
        }
        if (glyph) {
            CGAffineTransform transform;
            CGPathRef path;
            position = CGPointMake(x, text->y + ascent);
            transform = CGAffineTransformMake(
                glyph_scale, 0, 0, -glyph_scale,
                position.x, position.y);
            path = CTFontCreatePathForGlyph(
                quartz->text_font, glyph, &transform);
            if (path) {
                CGContextAddPath(context, path);
                CGContextFillPath(context);
                CGPathRelease(path);
            }
        }
        x += glyph_info.xadvance;
        offset += length;
    }
}

NK_INTERN void
nk_quartz_draw_image(CGContextRef context,
    const struct nk_command_image *command)
{
    CGImageRef image = (CGImageRef)command->img.handle.ptr;
    CGImageRef cropped = NULL;
    CGRect destination;
    if (!image || !command->w || !command->h) return;
    if (command->img.region[2] && command->img.region[3] &&
        command->img.w && command->img.h) {
        CGFloat scale_x = (CGFloat)CGImageGetWidth(image) / command->img.w;
        CGFloat scale_y = (CGFloat)CGImageGetHeight(image) / command->img.h;
        CGRect source = CGRectMake(
            command->img.region[0] * scale_x,
            command->img.region[1] * scale_y,
            command->img.region[2] * scale_x,
            command->img.region[3] * scale_y);
        cropped = CGImageCreateWithImageInRect(image, source);
        if (cropped) image = cropped;
    }
    destination = CGRectMake(command->x, command->y, command->w, command->h);
    CGContextSaveGState(context);
    CGContextBeginTransparencyLayerWithRect(context, destination, NULL);
    CGContextTranslateCTM(context, 0,
        destination.origin.y * 2.0 + destination.size.height);
    CGContextScaleCTM(context, 1.0, -1.0);
    CGContextDrawImage(context, destination, image);
    CGContextScaleCTM(context, 1.0, -1.0);
    CGContextTranslateCTM(context, 0,
        -(destination.origin.y * 2.0 + destination.size.height));
    if (command->col.r != 255 || command->col.g != 255 ||
        command->col.b != 255 || command->col.a != 255) {
        CGContextSetBlendMode(context, kCGBlendModeMultiply);
        nk_quartz_set_fill(context, command->col);
        CGContextFillRect(context, destination);
    }
    CGContextEndTransparencyLayer(context);
    CGContextRestoreGState(context);
    if (cropped) CGImageRelease(cropped);
}

NK_INTERN void
nk_quartz_draw_commands(struct nk_quartz *quartz, CGContextRef context,
    struct nk_context *ctx, CGSize viewport, struct nk_colorf clear,
    enum nk_anti_aliasing aa)
{
    const struct nk_command *command;
    CGContextSaveGState(context);
    CGContextSetRGBFillColor(context, clear.r, clear.g, clear.b, clear.a);
    CGContextFillRect(context, CGRectMake(0, 0, viewport.width, viewport.height));
    CGContextTranslateCTM(context, 0, viewport.height);
    CGContextScaleCTM(context, 1.0, -1.0);
    CGContextSetShouldAntialias(context, aa == NK_ANTI_ALIASING_ON);
    CGContextSetLineCap(context, kCGLineCapButt);
    CGContextSetLineJoin(context, kCGLineJoinMiter);
    CGContextSaveGState(context);
    nk_foreach(command, ctx) {
        switch (command->type) {
        case NK_COMMAND_NOP:
            break;
        case NK_COMMAND_SCISSOR: {
            const struct nk_command_scissor *scissor =
                (const struct nk_command_scissor *)command;
            CGContextRestoreGState(context);
            CGContextSaveGState(context);
            CGContextClipToRect(context,
                CGRectMake(scissor->x, scissor->y, scissor->w, scissor->h));
        } break;
        case NK_COMMAND_LINE: {
            const struct nk_command_line *line =
                (const struct nk_command_line *)command;
            nk_quartz_set_stroke(context, line->color, line->line_thickness);
            CGContextBeginPath(context);
            CGContextMoveToPoint(context, line->begin.x, line->begin.y);
            CGContextAddLineToPoint(context, line->end.x, line->end.y);
            CGContextStrokePath(context);
        } break;
        case NK_COMMAND_CURVE: {
            const struct nk_command_curve *curve =
                (const struct nk_command_curve *)command;
            nk_quartz_set_stroke(context, curve->color, curve->line_thickness);
            CGContextBeginPath(context);
            CGContextMoveToPoint(context, curve->begin.x, curve->begin.y);
            CGContextAddCurveToPoint(context,
                curve->ctrl[0].x, curve->ctrl[0].y,
                curve->ctrl[1].x, curve->ctrl[1].y,
                curve->end.x, curve->end.y);
            CGContextStrokePath(context);
        } break;
        case NK_COMMAND_RECT: {
            const struct nk_command_rect *rect =
                (const struct nk_command_rect *)command;
            CGRect bounds = CGRectMake(rect->x, rect->y, rect->w, rect->h);
            CGFloat rounding = rect->rounding;
            CGPathRef path = CGPathCreateWithRoundedRect(
                bounds, rounding, rounding, NULL);
            nk_quartz_set_stroke(context, rect->color, rect->line_thickness);
            CGContextAddPath(context, path);
            CGContextStrokePath(context);
            CGPathRelease(path);
        } break;
        case NK_COMMAND_RECT_FILLED: {
            const struct nk_command_rect_filled *rect =
                (const struct nk_command_rect_filled *)command;
            CGRect bounds = CGRectMake(rect->x, rect->y, rect->w, rect->h);
            CGFloat rounding = rect->rounding;
            CGPathRef path = CGPathCreateWithRoundedRect(
                bounds, rounding, rounding, NULL);
            nk_quartz_set_fill(context, rect->color);
            CGContextAddPath(context, path);
            CGContextFillPath(context);
            CGPathRelease(path);
        } break;
        case NK_COMMAND_RECT_MULTI_COLOR:
            nk_quartz_fill_multi_color(context,
                (const struct nk_command_rect_multi_color *)command);
            break;
        case NK_COMMAND_CIRCLE: {
            const struct nk_command_circle *circle =
                (const struct nk_command_circle *)command;
            CGRect bounds = CGRectMake(
                circle->x, circle->y, circle->w, circle->h);
            nk_quartz_set_stroke(context, circle->color,
                circle->line_thickness);
            CGContextStrokeEllipseInRect(context, bounds);
        } break;
        case NK_COMMAND_CIRCLE_FILLED: {
            const struct nk_command_circle_filled *circle =
                (const struct nk_command_circle_filled *)command;
            CGRect bounds = CGRectMake(
                circle->x, circle->y, circle->w, circle->h);
            nk_quartz_set_fill(context, circle->color);
            CGContextFillEllipseInRect(context, bounds);
        } break;
        case NK_COMMAND_ARC: {
            const struct nk_command_arc *arc =
                (const struct nk_command_arc *)command;
            CGContextBeginPath(context);
            CGContextMoveToPoint(context, arc->cx, arc->cy);
            CGContextAddArc(context, arc->cx, arc->cy, arc->r,
                arc->a[0], arc->a[1], 0);
            CGContextClosePath(context);
            nk_quartz_set_stroke(context, arc->color, arc->line_thickness);
            CGContextStrokePath(context);
        } break;
        case NK_COMMAND_ARC_FILLED: {
            const struct nk_command_arc_filled *arc =
                (const struct nk_command_arc_filled *)command;
            CGContextBeginPath(context);
            CGContextMoveToPoint(context, arc->cx, arc->cy);
            CGContextAddArc(context, arc->cx, arc->cy, arc->r,
                arc->a[0], arc->a[1], 0);
            CGContextClosePath(context);
            nk_quartz_set_fill(context, arc->color);
            CGContextFillPath(context);
        } break;
        case NK_COMMAND_TRIANGLE: {
            const struct nk_command_triangle *triangle =
                (const struct nk_command_triangle *)command;
            CGContextBeginPath(context);
            CGContextMoveToPoint(context, triangle->a.x, triangle->a.y);
            CGContextAddLineToPoint(context, triangle->b.x, triangle->b.y);
            CGContextAddLineToPoint(context, triangle->c.x, triangle->c.y);
            CGContextClosePath(context);
            nk_quartz_set_stroke(context, triangle->color,
                triangle->line_thickness);
            CGContextStrokePath(context);
        } break;
        case NK_COMMAND_TRIANGLE_FILLED: {
            const struct nk_command_triangle_filled *triangle =
                (const struct nk_command_triangle_filled *)command;
            CGContextBeginPath(context);
            CGContextMoveToPoint(context, triangle->a.x, triangle->a.y);
            CGContextAddLineToPoint(context, triangle->b.x, triangle->b.y);
            CGContextAddLineToPoint(context, triangle->c.x, triangle->c.y);
            CGContextClosePath(context);
            nk_quartz_set_fill(context, triangle->color);
            CGContextFillPath(context);
        } break;
        case NK_COMMAND_POLYGON:
        case NK_COMMAND_POLYLINE: {
            int closed = command->type == NK_COMMAND_POLYGON;
            const struct nk_command_polygon *polygon =
                (const struct nk_command_polygon *)command;
            unsigned short index;
            if (!polygon->point_count) break;
            CGContextBeginPath(context);
            CGContextMoveToPoint(context,
                polygon->points[0].x, polygon->points[0].y);
            for (index = 1; index < polygon->point_count; ++index) {
                CGContextAddLineToPoint(context,
                    polygon->points[index].x, polygon->points[index].y);
            }
            if (closed) CGContextClosePath(context);
            nk_quartz_set_stroke(context, polygon->color,
                polygon->line_thickness);
            CGContextStrokePath(context);
        } break;
        case NK_COMMAND_POLYGON_FILLED: {
            const struct nk_command_polygon_filled *polygon =
                (const struct nk_command_polygon_filled *)command;
            unsigned short index;
            if (!polygon->point_count) break;
            CGContextBeginPath(context);
            CGContextMoveToPoint(context,
                polygon->points[0].x, polygon->points[0].y);
            for (index = 1; index < polygon->point_count; ++index) {
                CGContextAddLineToPoint(context,
                    polygon->points[index].x, polygon->points[index].y);
            }
            CGContextClosePath(context);
            nk_quartz_set_fill(context, polygon->color);
            CGContextFillPath(context);
        } break;
        case NK_COMMAND_TEXT:
            nk_quartz_draw_text(quartz, context,
                (const struct nk_command_text *)command);
            break;
        case NK_COMMAND_IMAGE:
            nk_quartz_draw_image(context,
                (const struct nk_command_image *)command);
            break;
        case NK_COMMAND_CUSTOM: {
            const struct nk_command_custom *custom =
                (const struct nk_command_custom *)command;
            if (custom->callback) custom->callback(context,
                custom->x, custom->y, custom->w, custom->h,
                custom->callback_data);
        } break;
        default:
            break;
        }
    }
    CGContextRestoreGState(context);
    CGContextRestoreGState(context);
}

@implementation MorphNuklearQuartzView
- (BOOL)isOpaque
{
    return YES;
}

- (NSView *)hitTest:(NSPoint)point
{
    (void)point;
    return nil;
}

- (void)drawRect:(NSRect)dirtyRect
{
    struct nk_quartz *renderer = self.renderer;
    CGContextRef context = [[NSGraphicsContext currentContext] CGContext];
    (void)dirtyRect;
    if (!renderer || !renderer->drawing_context || !context) return;
    nk_quartz_draw_commands(renderer, context, renderer->drawing_context,
        self.bounds.size, renderer->clear, renderer->aa);
    renderer->did_draw = 1;
}
@end

NK_API int
nk_quartz_init(struct nk_quartz *quartz, SDL_Window *window)
{
    SDL_PropertiesID properties;
    NSWindow *cocoa_window;
    NSView *content;
    MorphNuklearQuartzView *view;
    if (!quartz || !window) return 0;
    NK_MEMSET(quartz, 0, sizeof(*quartz));
    properties = SDL_GetWindowProperties(window);
    cocoa_window = (__bridge NSWindow *)SDL_GetPointerProperty(properties,
        SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
    if (!cocoa_window || !(content = cocoa_window.contentView)) return 0;
    view = [[MorphNuklearQuartzView alloc] initWithFrame:content.bounds];
    if (!view) return 0;
    view.renderer = quartz;
    view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    view.wantsLayer = YES;
    [content addSubview:view positioned:NSWindowAbove relativeTo:nil];
    quartz->retained_view = (__bridge_retained void *)view;
    quartz->invalidated = 1;
    return 1;
}

NK_API void
nk_quartz_upload_atlas(struct nk_quartz *quartz, struct nk_font_atlas *atlas,
    struct nk_font *font)
{
    CFDataRef data;
    CGDataProviderRef provider;
    if (!quartz || !atlas || !font ||
        !font->config || !font->config->ttf_blob || !font->config->ttf_size) return;
    if (quartz->text_font) CFRelease(quartz->text_font);
    if (quartz->font) CGFontRelease(quartz->font);
    quartz->text_font = NULL;
    quartz->font = NULL;
    quartz->has_font_info = stbtt_InitFont(&quartz->font_info,
        font->config->ttf_blob,
        stbtt_GetFontOffsetForIndex(font->config->ttf_blob, 0));
    quartz->fallback_glyph = quartz->has_font_info
        ? (CGGlyph)stbtt_FindGlyphIndex(&quartz->font_info,
            (int)font->fallback_codepoint)
        : 0;
    data = CFDataCreate(kCFAllocatorDefault,
        font->config->ttf_blob, (CFIndex)font->config->ttf_size);
    provider = data ? CGDataProviderCreateWithCFData(data) : NULL;
    quartz->font = provider ? CGFontCreateWithDataProvider(provider) : NULL;
    if (quartz->font) {
        quartz->text_font = CTFontCreateWithGraphicsFont(quartz->font,
            font->handle.height, NULL, NULL);
    }
    quartz->nuklear_font = &font->handle;
    nk_font_atlas_end(atlas, nk_handle_ptr(quartz), NULL);
    if (provider) CGDataProviderRelease(provider);
    if (data) CFRelease(data);
}

NK_API void
nk_quartz_invalidate(struct nk_quartz *quartz)
{
    if (quartz) quartz->invalidated = 1;
}

NK_API void
nk_quartz_render(struct nk_quartz *quartz, struct nk_context *ctx,
    struct nk_colorf clear, enum nk_anti_aliasing aa)
{
    MorphNuklearQuartzView *view;
    CGSize viewport;
    int presentation_changed;
    if (!quartz || !ctx || !quartz->retained_view) {
        if (ctx) nk_clear(ctx);
        return;
    }
    view = (__bridge MorphNuklearQuartzView *)quartz->retained_view;
    viewport = view.bounds.size;
    quartz->last_frame_rendered = 0;
    quartz->attempted_frame_count++;
    presentation_changed = !quartz->has_presentation_state ||
        quartz->last_viewport_size.width != viewport.width ||
        quartz->last_viewport_size.height != viewport.height ||
        quartz->last_clear.r != clear.r || quartz->last_clear.g != clear.g ||
        quartz->last_clear.b != clear.b || quartz->last_clear.a != clear.a ||
        quartz->last_aa != aa;
    if (!quartz->invalidated && !presentation_changed &&
        morph_nuklear_frame_matches(&quartz->frame_cache, ctx)) {
        quartz->skipped_frame_count++;
        nk_clear(ctx);
        return;
    }
    quartz->drawing_context = ctx;
    quartz->clear = clear;
    quartz->aa = aa;
    quartz->did_draw = 0;
    [view setNeedsDisplay:YES];
    [view displayIfNeeded];
    if (!quartz->did_draw) [view display];
    quartz->drawing_context = NULL;
    if (quartz->did_draw) {
        quartz->last_viewport_size = viewport;
        quartz->last_clear = clear;
        quartz->last_aa = aa;
        quartz->has_presentation_state = 1;
        quartz->last_frame_rendered = 1;
        quartz->rendered_frame_count++;
        quartz->invalidated =
            !morph_nuklear_frame_store(&quartz->frame_cache, ctx);
    } else {
        quartz->invalidated = 1;
    }
    nk_clear(ctx);
}

NK_API void
nk_quartz_shutdown(struct nk_quartz *quartz)
{
    if (!quartz) return;
    if (quartz->retained_view) {
        MorphNuklearQuartzView *view =
            (__bridge_transfer MorphNuklearQuartzView *)quartz->retained_view;
        view.renderer = NULL;
        [view removeFromSuperview];
        quartz->retained_view = NULL;
    }
    morph_nuklear_frame_cache_free(&quartz->frame_cache);
    if (quartz->text_font) CFRelease(quartz->text_font);
    if (quartz->font) CGFontRelease(quartz->font);
    quartz->text_font = NULL;
    quartz->font = NULL;
}

#endif
#endif
