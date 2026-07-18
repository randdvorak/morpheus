#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_IMPLEMENTATION
#include "nuklear.h"

#define NK_METAL_IMPLEMENTATION
#include "nuklear_metal.h"

#include "runtime_module.h"

#define WINDOW_WIDTH 1200
#define WINDOW_HEIGHT 800

typedef struct morph_ui_context {
    struct nk_context *nuklear;
} morph_ui_context;

static void host_log(morph_host *host, const char *message)
{
    (void)host;
    SDL_Log("%s", message ? message : "");
}
static void host_ui_label(morph_host *host, const char *text)
{
    morph_ui_context *ui = (morph_ui_context *)host->user_data;
    nk_layout_row_dynamic(ui->nuklear, 24.0f, 1);
    nk_label(ui->nuklear, text ? text : "", NK_TEXT_LEFT);
}

static int host_ui_button(morph_host *host, const char *text)
{
    morph_ui_context *ui = (morph_ui_context *)host->user_data;
    nk_layout_row_dynamic(ui->nuklear, 30.0f, 1);
    return nk_button_label(ui->nuklear, text ? text : "");
}

static void handle_key(struct nk_context *ctx, const SDL_KeyboardEvent *event)
{
    const int down = event->down;
    const SDL_Keymod mods = event->mod;

    switch (event->key) {
    case SDLK_DELETE: nk_input_key(ctx, NK_KEY_DEL, down); break;
    case SDLK_RETURN: nk_input_key(ctx, NK_KEY_ENTER, down); break;
    case SDLK_TAB: nk_input_key(ctx, NK_KEY_TAB, down); break;
    case SDLK_BACKSPACE: nk_input_key(ctx, NK_KEY_BACKSPACE, down); break;
    case SDLK_UP: nk_input_key(ctx, NK_KEY_UP, down); break;
    case SDLK_DOWN: nk_input_key(ctx, NK_KEY_DOWN, down); break;
    case SDLK_LEFT:
        nk_input_key(ctx,
            (mods & SDL_KMOD_SHIFT) ? NK_KEY_TEXT_WORD_LEFT : NK_KEY_LEFT,
            down);
        break;
    case SDLK_RIGHT:
        nk_input_key(ctx,
            (mods & SDL_KMOD_SHIFT) ? NK_KEY_TEXT_WORD_RIGHT : NK_KEY_RIGHT,
            down);
        break;
    case SDLK_HOME: nk_input_key(ctx, NK_KEY_TEXT_START, down); break;
    case SDLK_END: nk_input_key(ctx, NK_KEY_TEXT_END, down); break;
    case SDLK_PAGEUP: nk_input_key(ctx, NK_KEY_SCROLL_UP, down); break;
    case SDLK_PAGEDOWN: nk_input_key(ctx, NK_KEY_SCROLL_DOWN, down); break;
    case SDLK_A:
        if (mods & SDL_KMOD_GUI) nk_input_key(ctx, NK_KEY_TEXT_SELECT_ALL, down);
        break;
    case SDLK_C:
        if (mods & SDL_KMOD_GUI) nk_input_key(ctx, NK_KEY_COPY, down);
        break;
    case SDLK_V:
        if (mods & SDL_KMOD_GUI) nk_input_key(ctx, NK_KEY_PASTE, down);
        break;
    case SDLK_X:
        if (mods & SDL_KMOD_GUI) nk_input_key(ctx, NK_KEY_CUT, down);
        break;
    case SDLK_Z:
        if (mods & SDL_KMOD_GUI) {
            nk_input_key(ctx,
                (mods & SDL_KMOD_SHIFT) ? NK_KEY_TEXT_REDO : NK_KEY_TEXT_UNDO,
                down);
        }
        break;
    default: break;
    }

    nk_input_key(ctx, NK_KEY_SHIFT, (mods & SDL_KMOD_SHIFT) != 0);
    nk_input_key(ctx, NK_KEY_CTRL, (mods & SDL_KMOD_CTRL) != 0);
}

static void handle_event(struct nk_context *ctx, const SDL_Event *event)
{
    switch (event->type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        handle_key(ctx, &event->key);
        break;
    case SDL_EVENT_TEXT_INPUT:
        nk_input_glyph(ctx, event->text.text);
        break;
    case SDL_EVENT_MOUSE_MOTION:
        nk_input_motion(ctx, (int)event->motion.x, (int)event->motion.y);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        enum nk_buttons button;
        if (event->button.button == SDL_BUTTON_LEFT) button = NK_BUTTON_LEFT;
        else if (event->button.button == SDL_BUTTON_MIDDLE) button = NK_BUTTON_MIDDLE;
        else if (event->button.button == SDL_BUTTON_RIGHT) button = NK_BUTTON_RIGHT;
        else break;
        nk_input_button(ctx, button,
            (int)event->button.x,
            (int)event->button.y,
            event->button.down);
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL:
        nk_input_scroll(ctx, nk_vec2(event->wheel.x, event->wheel.y));
        break;
    default:
        break;
    }
}

static void clipboard_paste(nk_handle user, struct nk_text_edit *edit)
{
    char *text;
    (void)user;

    text = SDL_GetClipboardText();
    if (text) {
        nk_textedit_paste(edit, text, (int)SDL_strlen(text));
        SDL_free(text);
    }
}

static void clipboard_copy(nk_handle user, const char *text, int length)
{
    char *copy;
    (void)user;

    if (length <= 0) return;
    copy = (char *)SDL_malloc((size_t)length + 1);
    if (!copy) return;
    SDL_memcpy(copy, text, (size_t)length);
    copy[length] = '\0';
    SDL_SetClipboardText(copy);
    SDL_free(copy);
}

int main(int argc, char **argv)
{
    SDL_Window *window = NULL;
    SDL_MetalView view = NULL;
    CAMetalLayer *layer;
    struct nk_context ctx;
    struct nk_font_atlas atlas;
    struct nk_font *font;
    struct nk_metal metal;
    struct nk_colorf background = {0.06f, 0.08f, 0.11f, 1.0f};
    morph_ui_context ui;
    morph_host host;
    morph_runtime_module module;
    char module_error[4096] = {0};
    const char *module_source = MORPHEUS_SOURCE_ROOT "/generated/app.c";
    const void *pixels;
    int atlas_width;
    int atlas_height;
    int running = 1;
    int exit_code = EXIT_FAILURE;
    float font_scale;
    Uint64 previous_ticks;

    (void)argc;
    (void)argv;

    morph_runtime_module_init(&module);

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return EXIT_FAILURE;
    }

    window = SDL_CreateWindow("Morpheus",
        WINDOW_WIDTH,
        WINDOW_HEIGHT,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_METAL);
    if (!window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        goto shutdown_sdl;
    }

    view = SDL_Metal_CreateView(window);
    if (!view) {
        SDL_Log("SDL_Metal_CreateView failed: %s", SDL_GetError());
        goto shutdown_window;
    }

    layer = (__bridge CAMetalLayer *)SDL_Metal_GetLayer(view);
    if (!nk_metal_init(&metal, MTLPixelFormatBGRA8Unorm)) {
        SDL_Log("Unable to initialize Nuklear Metal renderer");
        goto shutdown_view;
    }
    layer.device = metal.device;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = YES;
    layer.displaySyncEnabled = YES;

    nk_init_default(&ctx, NULL);
    ctx.clip.copy = clipboard_copy;
    ctx.clip.paste = clipboard_paste;
    font_scale = SDL_GetWindowDisplayScale(window);
    nk_font_atlas_init_default(&atlas);
    nk_font_atlas_begin(&atlas);
    font = nk_font_atlas_add_default(&atlas, 15.0f * font_scale, NULL);
    pixels = nk_font_atlas_bake(
        &atlas,
        &atlas_width,
        &atlas_height,
        NK_FONT_ATLAS_RGBA32);
    nk_metal_upload_atlas(
        &metal,
        pixels,
        atlas_width,
        atlas_height,
        &atlas);
    font->handle.height /= font_scale;
    nk_style_set_font(&ctx, &font->handle);
    SDL_StartTextInput(window);

    ui.nuklear = &ctx;
    host.abi_version = MORPHEUS_HOST_ABI_VERSION;
    host.user_data = &ui;
    host.log = host_log;
    host.ui_label = host_ui_label;
    host.ui_button = host_ui_button;

    if (!morph_runtime_module_reload(
            &module,
            &host,
            module_source,
            module_error,
            sizeof(module_error))) {
        SDL_Log("Initial module failed: %s", module_error);
    }

    previous_ticks = SDL_GetTicksNS();
    while (running) {
        SDL_Event event;
        Uint64 ticks = SDL_GetTicksNS();
        double dt = (double)(ticks - previous_ticks) / 1000000000.0;
        int pixel_width;
        int pixel_height;
        int window_width;
        int window_height;
        previous_ticks = ticks;

        nk_input_begin(&ctx);
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_KEY_DOWN &&
                 event.key.key == SDLK_Q &&
                 (event.key.mod & SDL_KMOD_GUI))) {
                running = 0;
            }
            handle_event(&ctx, &event);
        }
        nk_input_end(&ctx);

        morph_runtime_module_update(&module, &host, dt);

        if (nk_begin(&ctx,
                "Morpheus Host",
                nk_rect(30, 30, 430, 260),
                NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE |
                NK_WINDOW_MINIMIZABLE | NK_WINDOW_TITLE)) {
            nk_layout_row_dynamic(&ctx, 28.0f, 1);
            nk_label(&ctx, "Native host: SDL3 + Metal + Nuklear", NK_TEXT_LEFT);
            nk_label(&ctx,
                module.api ? "Runtime C module: active" : "Runtime C module: unavailable",
                NK_TEXT_LEFT);
            if (module.api && module.api->name) {
                nk_label(&ctx, module.api->name, NK_TEXT_LEFT);
            }

            nk_layout_row_dynamic(&ctx, 34.0f, 1);
            if (nk_button_label(&ctx, "Recompile generated/app.c")) {
                if (morph_runtime_module_reload(
                        &module,
                        &host,
                        module_source,
                        module_error,
                        sizeof(module_error))) {
                    module_error[0] = '\0';
                }
            }

            if (module_error[0]) {
                nk_layout_row_dynamic(&ctx, 90.0f, 1);
                nk_label_wrap(&ctx, module_error);
            }
        }
        nk_end(&ctx);

        if (nk_begin(&ctx,
                "Generated Application",
                nk_rect(500, 30, 520, 300),
                NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE |
                NK_WINDOW_MINIMIZABLE | NK_WINDOW_TITLE)) {
            morph_runtime_module_render_ui(&module, &host);
        }
        nk_end(&ctx);

        SDL_GetWindowSize(window, &window_width, &window_height);
        SDL_GetWindowSizeInPixels(window, &pixel_width, &pixel_height);
        layer.drawableSize = CGSizeMake(pixel_width, pixel_height);
        if (window_width > 0 && window_height > 0) {
            nk_metal_render(
                &metal,
                &ctx,
                layer,
                background,
                NK_ANTI_ALIASING_ON);
        }
    }

    exit_code = EXIT_SUCCESS;
    morph_runtime_module_destroy(&module, &host);
    SDL_StopTextInput(window);
    nk_font_atlas_clear(&atlas);
    nk_free(&ctx);
    nk_metal_shutdown(&metal);

shutdown_view:
    if (view) SDL_Metal_DestroyView(view);
shutdown_window:
    if (window) SDL_DestroyWindow(window);
shutdown_sdl:
    SDL_Quit();
    return exit_code;
}
