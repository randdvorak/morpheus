#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <Foundation/Foundation.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_INCLUDE_COMMAND_USERDATA
#define NK_UINT_DRAW_INDEX
#define NK_IMPLEMENTATION
#include "nuklear.h"

_Static_assert(sizeof(nk_draw_index) == 4,
    "Morpheus requires 32-bit Nuklear draw indices");

#define NK_METAL_IMPLEMENTATION
#include "nuklear_metal.h"

#include "morpheus/app_api.h"
#include "morpheus/runtime.h"
#include "http_service.h"
#include "image_service.h"
#include "theme.h"

#define MORPH_DEFAULT_WINDOW_WIDTH 1000
#define MORPH_DEFAULT_WINDOW_HEIGHT 700
#define MORPH_STATE_MAGIC 0x4d535431u
#define MORPH_STATE_FORMAT 1u

typedef struct morph_state_header {
    uint32_t magic;
    uint32_t format;
    uint32_t app_abi;
    uint32_t reserved;
    uint64_t payload_size;
} morph_state_header;

typedef struct morph_export_ui {
    struct nk_context *nuklear;
} morph_export_ui;

static void host_log(morph_host *host, const char *message)
{
    (void)host;
    SDL_Log("%s", message ? message : "");
}

static void host_ui_label(morph_host *host, const char *text)
{
    morph_export_ui *ui = host->user_data;
    nk_layout_row_dynamic(ui->nuklear, 24.0f, 1);
    nk_label(ui->nuklear, text ? text : "", NK_TEXT_LEFT);
}

static int host_ui_button(morph_host *host, const char *text)
{
    morph_export_ui *ui = host->user_data;
    nk_layout_row_dynamic(ui->nuklear, 30.0f, 1);
    return nk_button_label(ui->nuklear, text ? text : "");
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
    copy = SDL_malloc((size_t)length + 1u);
    if (!copy) return;
    SDL_memcpy(copy, text, (size_t)length);
    copy[length] = '\0';
    SDL_SetClipboardText(copy);
    SDL_free(copy);
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
    case SDLK_LEFT: nk_input_key(ctx, (mods & SDL_KMOD_SHIFT) ? NK_KEY_TEXT_WORD_LEFT : NK_KEY_LEFT, down); break;
    case SDLK_RIGHT: nk_input_key(ctx, (mods & SDL_KMOD_SHIFT) ? NK_KEY_TEXT_WORD_RIGHT : NK_KEY_RIGHT, down); break;
    case SDLK_HOME: nk_input_key(ctx, NK_KEY_TEXT_START, down); break;
    case SDLK_END: nk_input_key(ctx, NK_KEY_TEXT_END, down); break;
    case SDLK_PAGEUP: nk_input_key(ctx, NK_KEY_SCROLL_UP, down); break;
    case SDLK_PAGEDOWN: nk_input_key(ctx, NK_KEY_SCROLL_DOWN, down); break;
    case SDLK_A: if (mods & SDL_KMOD_GUI) nk_input_key(ctx, NK_KEY_TEXT_SELECT_ALL, down); break;
    case SDLK_C: if (mods & SDL_KMOD_GUI) nk_input_key(ctx, NK_KEY_COPY, down); break;
    case SDLK_V: if (mods & SDL_KMOD_GUI) nk_input_key(ctx, NK_KEY_PASTE, down); break;
    case SDLK_X: if (mods & SDL_KMOD_GUI) nk_input_key(ctx, NK_KEY_CUT, down); break;
    case SDLK_Z:
        if (mods & SDL_KMOD_GUI) nk_input_key(ctx,
            (mods & SDL_KMOD_SHIFT) ? NK_KEY_TEXT_REDO : NK_KEY_TEXT_UNDO, down);
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
    case SDL_EVENT_KEY_UP: handle_key(ctx, &event->key); break;
    case SDL_EVENT_TEXT_INPUT: nk_input_glyph(ctx, event->text.text); break;
    case SDL_EVENT_MOUSE_MOTION:
        nk_input_motion(ctx, (int)event->motion.x, (int)event->motion.y); break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        enum nk_buttons button;
        if (event->button.button == SDL_BUTTON_LEFT) button = NK_BUTTON_LEFT;
        else if (event->button.button == SDL_BUTTON_MIDDLE) button = NK_BUTTON_MIDDLE;
        else if (event->button.button == SDL_BUTTON_RIGHT) button = NK_BUTTON_RIGHT;
        else break;
        nk_input_button(ctx, button, (int)event->button.x, (int)event->button.y,
            event->button.down);
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL:
        nk_input_scroll(ctx, nk_vec2(event->wheel.x, event->wheel.y)); break;
    default: break;
    }
}

static NSURL *state_file_url(const morph_runtime_config *config)
{
    NSFileManager *manager = NSFileManager.defaultManager;
    const char *override = getenv("MORPHEUS_APP_SUPPORT_DIR");
    NSURL *support = override && *override
        ? [NSURL fileURLWithPath:[NSString stringWithUTF8String:override] isDirectory:YES]
        : [manager URLForDirectory:NSApplicationSupportDirectory
            inDomain:NSUserDomainMask appropriateForURL:nil create:YES error:nil];
    NSString *identifier = NSBundle.mainBundle.bundleIdentifier;
    if (!identifier && config && config->fallback_bundle_identifier) {
        identifier = [NSString
            stringWithUTF8String:config->fallback_bundle_identifier];
    }
    if (!identifier) identifier = @"dev.morpheus.exported";
    NSURL *directory = [support URLByAppendingPathComponent:identifier isDirectory:YES];
    if (![manager createDirectoryAtURL:directory withIntermediateDirectories:YES
            attributes:nil error:nil]) return nil;
    return [directory URLByAppendingPathComponent:@"state.bin" isDirectory:NO];
}

static void restore_state(
    const morph_app_api *api,
    morph_host *host,
    void **state,
    const morph_runtime_config *config)
{
    NSData *data = [NSData dataWithContentsOfURL:state_file_url(config)];
    morph_state_header header;
    if (!data || data.length < sizeof(header) || !api->load_state) return;
    memcpy(&header, data.bytes, sizeof(header));
    if (header.magic != MORPH_STATE_MAGIC || header.format != MORPH_STATE_FORMAT ||
        header.app_abi != MORPHEUS_APP_ABI_VERSION ||
        header.payload_size != (uint64_t)(data.length - sizeof(header))) return;
    (void)api->load_state(host, state,
        (const unsigned char *)data.bytes + sizeof(header), (unsigned long)header.payload_size);
}

static void save_state(
    const morph_app_api *api,
    morph_host *host,
    void *state,
    const morph_runtime_config *config)
{
    const void *payload = NULL;
    unsigned long payload_size = 0;
    morph_state_header header = {
        MORPH_STATE_MAGIC, MORPH_STATE_FORMAT, MORPHEUS_APP_ABI_VERSION, 0, 0
    };
    NSMutableData *data;
    NSURL *url;
    if (!api->save_state || !api->save_state(host, state, &payload, &payload_size) ||
        (payload_size && !payload)) return;
    header.payload_size = payload_size;
    data = [NSMutableData dataWithBytes:&header length:sizeof(header)];
    if (payload_size) [data appendBytes:payload length:(NSUInteger)payload_size];
    url = state_file_url(config);
    if (url && ![data writeToURL:url options:NSDataWritingAtomic error:nil]) {
        SDL_Log("Unable to save application state");
    }
}

int morph_runtime_run(
    const morph_app_api *api,
    const morph_runtime_config *config)
{
    SDL_Window *window = NULL;
    SDL_MetalView view = NULL;
    CAMetalLayer *layer;
    struct nk_context ctx;
    struct nk_font_atlas atlas;
    struct nk_font *font;
    struct nk_metal metal;
    struct nk_colorf background = {0.22f, 0.27f, 0.33f, 1.0f};
    morph_export_ui ui;
    morph_host host = {0};
    morph_http_service *http = NULL;
    morph_image_service *images = NULL;
    const char *fallback_name = "Morpheus App";
    unsigned int render_mode = MORPHEUS_RENDER_EMBEDDED;
    int initial_width = MORPH_DEFAULT_WINDOW_WIDTH;
    int initial_height = MORPH_DEFAULT_WINDOW_HEIGHT;
    void *state = NULL;
    const void *pixels;
    int atlas_width;
    int atlas_height;
    int running = 1;
    float font_scale;
    Uint64 previous_ticks;
    int exit_code = EXIT_FAILURE;

    if (config) {
        if (config->fallback_name) fallback_name = config->fallback_name;
        render_mode = config->render_mode;
        if (config->window_width > 0) initial_width = config->window_width;
        if (config->window_height > 0) initial_height = config->window_height;
    }
    if (!api || api->abi_version != MORPHEUS_APP_ABI_VERSION || !api->create ||
        !api->destroy || !api->update || !api->render_ui) {
        fprintf(stderr, "Exported application ABI is invalid\n");
        return EXIT_FAILURE;
    }
    if (render_mode > MORPHEUS_RENDER_NUKLEAR_WINDOWS) {
        fprintf(stderr, "Exported application render mode is invalid\n");
        return EXIT_FAILURE;
    }
    {
        const char *validate_only = getenv("MORPHEUS_RUNTIME_VALIDATE_ONLY");
        if (validate_only && strcmp(validate_only, "1") == 0) {
            return EXIT_SUCCESS;
        }
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) return EXIT_FAILURE;
    window = SDL_CreateWindow(api->name ? api->name : fallback_name,
        initial_width, initial_height,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_METAL);
    if (!window) goto shutdown_sdl;
    view = SDL_Metal_CreateView(window);
    if (!view) goto shutdown_window;
    layer = (__bridge CAMetalLayer *)SDL_Metal_GetLayer(view);
    if (!nk_metal_init(&metal, MTLPixelFormatBGRA8Unorm)) goto shutdown_view;
    layer.device = metal.device;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = YES;
    layer.displaySyncEnabled = YES;

    nk_init_default(&ctx, NULL);
    morph_apply_light_theme(&ctx);
    ctx.clip.copy = clipboard_copy;
    ctx.clip.paste = clipboard_paste;
    font_scale = SDL_GetWindowDisplayScale(window);
    nk_font_atlas_init_default(&atlas);
    nk_font_atlas_begin(&atlas);
    font = nk_font_atlas_add_default(&atlas, 15.0f * font_scale, NULL);
    pixels = nk_font_atlas_bake(&atlas, &atlas_width, &atlas_height, NK_FONT_ATLAS_RGBA32);
    nk_metal_upload_atlas(&metal, pixels, atlas_width, atlas_height, &atlas);
    font->handle.height /= font_scale;
    nk_style_set_font(&ctx, &font->handle);
    SDL_StartTextInput(window);

    http = morph_http_service_create();
    images = morph_image_service_create((__bridge void *)metal.device, &ctx, http);
    ui.nuklear = &ctx;
    host.abi_version = MORPHEUS_HOST_ABI_VERSION;
    host.user_data = &ui;
    host.log = host_log;
    host.ui_label = host_ui_label;
    host.ui_button = host_ui_button;
    host.nuklear = &ctx;
    host.http = http;
    host.images = images;
    host.capabilities = config ? config->capabilities : NULL;
    if (!api->create(&host, &state)) goto shutdown_ui;
    restore_state(api, &host, &state, config);

    previous_ticks = SDL_GetTicksNS();
    while (running) {
        SDL_Event event;
        Uint64 ticks = SDL_GetTicksNS();
        double dt = (double)(ticks - previous_ticks) / 1000000000.0;
        int pixel_width, pixel_height, window_width, window_height;
        previous_ticks = ticks;
        nk_input_begin(&ctx);
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = 0;
            handle_event(&ctx, &event);
        }
        nk_input_end(&ctx);
        morph_http_service_tick(http);
        morph_image_service_tick(images);
        api->update(&host, state, dt);
        if (render_mode == MORPHEUS_RENDER_NUKLEAR_WINDOWS) {
            api->render_ui(&host, state);
        } else {
            if (nk_begin(&ctx, api->name ? api->name : fallback_name,
                    nk_rect(20, 20, initial_width - 40, initial_height - 40),
                    NK_WINDOW_BORDER | NK_WINDOW_SCALABLE | NK_WINDOW_TITLE)) {
                api->render_ui(&host, state);
            }
            nk_end(&ctx);
        }
        SDL_GetWindowSize(window, &window_width, &window_height);
        SDL_GetWindowSizeInPixels(window, &pixel_width, &pixel_height);
        layer.drawableSize = CGSizeMake(pixel_width, pixel_height);
        if (window_width > 0 && window_height > 0) {
            nk_metal_render(&metal, &ctx, layer, background, NK_ANTI_ALIASING_ON);
        }
    }
    save_state(api, &host, state, config);
    api->destroy(&host, state);
    exit_code = EXIT_SUCCESS;

shutdown_ui:
    morph_image_service_destroy(images);
    morph_http_service_destroy(http);
    SDL_StopTextInput(window);
    nk_font_atlas_clear(&atlas);
    nk_free(&ctx);
    nk_metal_shutdown(&metal);
shutdown_view:
    SDL_Metal_DestroyView(view);
shutdown_window:
    SDL_DestroyWindow(window);
shutdown_sdl:
    SDL_Quit();
    return exit_code;
}
