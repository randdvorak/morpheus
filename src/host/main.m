#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>
#import <Foundation/Foundation.h>

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
#include "morpheus_nuklear_metal.h"

#include "morpheus/authoring.h"
#include "morpheus/authoring_app.h"
#include "authoring_capabilities.h"
#include "authoring_controller.h"
#include "authoring_shell.h"
#include "agent_session.h"
#include "export_service.h"
#include "http_service.h"
#include "image_service.h"
#include "project_store.h"
#include "revision_store.h"
#include "runtime_module.h"
#include "theme.h"

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

static int authoring_storage_root(char *output, unsigned long capacity)
{
    const char *override = getenv("MORPHEUS_AUTHORING_STATE_ROOT");
    NSFileManager *manager;
    NSURL *support;
    NSURL *directory;
    const char *path;
    int length;
    if (override && *override) {
        length = snprintf(output, (size_t)capacity, "%s", override);
        return length >= 0 && (unsigned long)length < capacity;
    }
    manager = NSFileManager.defaultManager;
    support = [manager URLForDirectory:NSApplicationSupportDirectory
        inDomain:NSUserDomainMask appropriateForURL:nil create:YES error:nil];
    directory = [[support URLByAppendingPathComponent:@"dev.morpheus.seed"
        isDirectory:YES] URLByAppendingPathComponent:@"authoring" isDirectory:YES];
    if (!directory || ![manager createDirectoryAtURL:directory
            withIntermediateDirectories:YES attributes:nil error:nil]) return 0;
    path = directory.fileSystemRepresentation;
    length = snprintf(output, (size_t)capacity, "%s", path ? path : "");
    return length >= 0 && (unsigned long)length < capacity;
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
    struct nk_colorf background = {0.22f, 0.27f, 0.33f, 1.0f};
    morph_ui_context ui;
    morph_host host = {0};
    morph_host authoring_host = {0};
    morph_runtime_module module;
    morph_authoring_controller authoring_controller = {0};
    morph_revision_store revisions;
    morph_project_store projects;
    morph_agent_session agent_session;
    morph_export_service export_service;
    morph_capability entries[6] = {{0}};
    morph_capability_registry registry = {entries, 0};
    morph_authoring_shell authoring_shell = {0};
    morph_http_service *http_service = NULL;
    morph_image_service *image_service = NULL;
    morph_authoring_controller_config controller_config = {0};
    char module_source[MORPHEUS_AUTHORING_CONTROLLER_PATH_CAPACITY];
    char workspace_root[MORPHEUS_AUTHORING_CONTROLLER_PATH_CAPACITY];
    char assets_root[MORPHEUS_AUTHORING_CONTROLLER_PATH_CAPACITY];
    char projects_root[MORPHEUS_AUTHORING_CONTROLLER_PATH_CAPACITY];
    char authoring_state_root[MORPHEUS_AUTHORING_SHELL_PATH_CAPACITY];
    char error[MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY] = {0};
    char ollama_model[MORPHEUS_AUTHORING_AGENT_MODEL_CAPACITY] = {0};
    const char *agent_provider_path;
    const char *agent_backend;
    const char *configured_ollama_model;
    const void *pixels;
    unsigned long capability_count = 0;
    int projects_enabled = 0;
    int agent_provider_is_custom;
    int agent_uses_ollama = 0;
    int controller_initialized = 0;
    int authoring_safe_mode = 0;
    int authoring_shell_ready = 0;
    int running = 1;
    int exit_code = EXIT_FAILURE;
    int atlas_width;
    int atlas_height;
    float font_scale;
    Uint64 previous_ticks;

    (void)argc;
    (void)argv;

    {
        const char *module_override = getenv("MORPHEUS_MODULE_SOURCE");
        const char *workspace_override = getenv("MORPHEUS_WORKSPACE_ROOT");
        if (module_override && *module_override &&
            workspace_override && *workspace_override) {
            snprintf(module_source, sizeof(module_source), "%s", module_override);
            snprintf(workspace_root, sizeof(workspace_root), "%s", workspace_override);
            snprintf(assets_root, sizeof(assets_root), "%s/assets", workspace_root);
        } else {
            const char *root_override = getenv("MORPHEUS_PROJECTS_ROOT");
            morph_host lookup = {0};
            const morph_capability *provider;
            const morph_authoring_projects_api *api;
            void *context;
            snprintf(projects_root, sizeof(projects_root), "%s",
                root_override && *root_override
                    ? root_override : MORPHEUS_SOURCE_ROOT "/projects");
            entries[capability_count++] = morph_authoring_projects_capability(&projects);
            registry.count = capability_count;
            lookup.abi_version = MORPHEUS_HOST_ABI_VERSION;
            lookup.capabilities = &registry;
            provider = morph_host_find_capability(&lookup,
                MORPHEUS_AUTHORING_PROJECTS_CAPABILITY,
                MORPHEUS_AUTHORING_PROJECTS_ABI_VERSION);
            api = morph_authoring_projects_from_capability(provider);
            context = provider ? provider->context : NULL;
            projects_enabled = api && api->init(
                context, projects_root, error, sizeof(error));
            if (!projects_enabled || !api->paths(
                    context,
                    workspace_root, sizeof(workspace_root),
                    module_source, sizeof(module_source),
                    assets_root, sizeof(assets_root))) {
                fprintf(stderr, "Unable to initialize projects: %s\n", error);
                return EXIT_FAILURE;
            }
        }
    }

    entries[capability_count++] = morph_authoring_revisions_capability(&revisions);
    entries[capability_count++] = morph_authoring_modules_capability(&module);
    entries[capability_count++] = morph_authoring_agent_capability(&agent_session);
    entries[capability_count++] = morph_authoring_export_capability(&export_service, NULL);
    registry.count = capability_count;

    agent_provider_path = getenv("MORPHEUS_AGENT_PROVIDER");
    agent_provider_is_custom = agent_provider_path && *agent_provider_path;
    agent_backend = getenv("MORPHEUS_AGENT_BACKEND");
    if (!agent_provider_is_custom) {
        agent_uses_ollama = agent_backend && strcmp(agent_backend, "ollama") == 0;
        agent_provider_path = agent_uses_ollama
            ? MORPHEUS_OLLAMA_PROVIDER_PATH : MORPHEUS_AGENT_PROVIDER_PATH;
    }
    configured_ollama_model = getenv("MORPHEUS_OLLAMA_MODEL");
    if (configured_ollama_model && *configured_ollama_model) {
        snprintf(ollama_model, sizeof(ollama_model), "%s", configured_ollama_model);
    }
    {
        const char *safe_mode = getenv("MORPHEUS_SAFE_MODE");
        authoring_safe_mode = safe_mode &&
            (strcmp(safe_mode, "1") == 0 || strcmp(safe_mode, "true") == 0);
    }
    if (!authoring_storage_root(
            authoring_state_root, sizeof(authoring_state_root))) {
        fprintf(stderr, "Unable to resolve authoring recovery directory\n");
        return EXIT_FAILURE;
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return EXIT_FAILURE;
    }
    window = SDL_CreateWindow("Morpheus", WINDOW_WIDTH, WINDOW_HEIGHT,
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
    morph_apply_light_theme(&ctx);
    ctx.clip.copy = clipboard_copy;
    ctx.clip.paste = clipboard_paste;
    font_scale = SDL_GetWindowDisplayScale(window);
    nk_font_atlas_init_default(&atlas);
    nk_font_atlas_begin(&atlas);
    font = nk_font_atlas_add_default(&atlas, 15.0f * font_scale, NULL);
    pixels = nk_font_atlas_bake(&atlas, &atlas_width, &atlas_height,
        NK_FONT_ATLAS_RGBA32);
    nk_metal_upload_atlas(&metal, pixels, atlas_width, atlas_height, &atlas);
    font->handle.height /= font_scale;
    nk_style_set_font(&ctx, &font->handle);
    SDL_StartTextInput(window);

    ui.nuklear = &ctx;
    host.abi_version = MORPHEUS_HOST_ABI_VERSION;
    host.user_data = &ui;
    host.log = host_log;
    host.ui_label = host_ui_label;
    host.ui_button = host_ui_button;
    host.nuklear = &ctx;
    http_service = morph_http_service_create();
    host.http = http_service;
    image_service = morph_image_service_create(
        (__bridge void *)metal.device, &ctx, http_service);
    host.images = image_service;

    authoring_host = host;
    authoring_host.capabilities = &registry;
    controller_initialized = morph_authoring_controller_init(
        &authoring_controller, &registry, &host);
    if (!controller_initialized) {
        SDL_Log("Morpheus authoring controller failed to initialize");
        goto shutdown_runtime;
    }
    controller_config.projects_enabled = projects_enabled;
    controller_config.workspace_root = workspace_root;
    controller_config.source_path = module_source;
    controller_config.assets_root = assets_root;
    controller_config.agent_provider_path = agent_provider_path;
    controller_config.codex_provider_path = MORPHEUS_AGENT_PROVIDER_PATH;
    controller_config.ollama_provider_path = MORPHEUS_OLLAMA_PROVIDER_PATH;
    controller_config.ollama_model = ollama_model;
    controller_config.api_header_path =
        MORPHEUS_SOURCE_ROOT "/include/morpheus/app_api.h";
    controller_config.sdk_header_path =
        MORPHEUS_SOURCE_ROOT "/include/morpheus/sdk.h";
    controller_config.agent_provider_is_custom = agent_provider_is_custom;
    controller_config.agent_uses_ollama = agent_uses_ollama;
    if (!morph_authoring_controller_start(
            &authoring_controller, &controller_config, error, sizeof(error))) {
        SDL_Log("Initial project failed to load: %s", error);
    }
    entries[capability_count++] =
        morph_authoring_controller_capability(&authoring_controller);
    registry.count = capability_count;

    {
        const char *authoring_source = getenv("MORPHEUS_AUTHORING_UI_SOURCE");
        if (!authoring_source || !*authoring_source) {
            authoring_source = MORPHEUS_AUTHORING_UI_SOURCE_PATH;
        }
        authoring_shell_ready = morph_authoring_shell_init(
            &authoring_shell,
            &authoring_host,
            morph_authoring_app_entry(),
            authoring_source,
            authoring_state_root,
            authoring_safe_mode,
            error,
            sizeof(error));
    }
    if (!authoring_shell_ready) {
        SDL_Log("Morpheus authoring shell failed to initialize: %s", error);
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
                 (event.key.mod & SDL_KMOD_GUI))) running = 0;
            handle_event(&ctx, &event);
        }
        nk_input_end(&ctx);

        morph_http_service_tick(http_service);
        morph_image_service_tick(image_service);
        (void)morph_authoring_controller_tick(&authoring_controller);
        morph_authoring_controller_update_runtime(&authoring_controller, dt);
        if (authoring_shell_ready) morph_authoring_shell_update(&authoring_shell, dt);

        if (nk_begin(&ctx, "Morpheus Host", nk_rect(30, 30, 430, 730),
                NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE |
                NK_WINDOW_MINIMIZABLE | NK_WINDOW_TITLE)) {
            if (authoring_shell_ready) {
                morph_authoring_shell_snapshot shell_snapshot = {
                    .struct_size = sizeof(shell_snapshot)
                };
                if (morph_authoring_shell_snapshot_get(
                        &authoring_shell, &shell_snapshot)) {
                    nk_layout_row_dynamic(&ctx, 22.0f, 1);
                    nk_label(&ctx, shell_snapshot.message, NK_TEXT_LEFT);
                    if (shell_snapshot.state == MORPHEUS_AUTHORING_SHELL_PREVIEW) {
                        nk_layout_row_dynamic(&ctx, 30.0f, 2);
                        if (nk_button_label(&ctx, "Accept authoring UI")) {
                            (void)morph_authoring_shell_accept(
                                &authoring_shell, error, sizeof(error));
                        }
                        if (nk_button_label(&ctx, "Rollback authoring UI")) {
                            (void)morph_authoring_shell_rollback(
                                &authoring_shell, error, sizeof(error));
                        }
                    } else {
                        nk_layout_row_dynamic(&ctx, 30.0f,
                            shell_snapshot.can_rollback ? 2 : 1);
                        if (nk_button_label(&ctx,
                                shell_snapshot.state ==
                                    MORPHEUS_AUTHORING_SHELL_ACCEPTED
                                    ? "Reload authoring source"
                                    : "Preview authoring source")) {
                            (void)morph_authoring_shell_preview(
                                &authoring_shell, error, sizeof(error));
                        }
                        if (shell_snapshot.can_rollback &&
                            nk_button_label(&ctx, "Use bootstrap UI")) {
                            (void)morph_authoring_shell_rollback(
                                &authoring_shell, error, sizeof(error));
                        }
                    }
                }
                morph_authoring_shell_render(&authoring_shell);
            } else {
                nk_layout_row_dynamic(&ctx, 24.0f, 1);
                nk_label(&ctx, "Immutable recovery shell: authoring UI unavailable",
                    NK_TEXT_LEFT);
            }
        }
        nk_end(&ctx);

        if (morph_authoring_controller_has_pending(&authoring_controller)) {
            (void)morph_authoring_controller_tick(&authoring_controller);
        }
        if (morph_authoring_controller_render_mode(&authoring_controller) ==
                MORPHEUS_RENDER_NUKLEAR_WINDOWS) {
            morph_authoring_controller_render_runtime(&authoring_controller);
        } else {
            if (nk_begin(&ctx, "Generated Application", nk_rect(500, 30, 520, 300),
                    NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE |
                    NK_WINDOW_MINIMIZABLE | NK_WINDOW_TITLE)) {
                morph_authoring_controller_render_runtime(&authoring_controller);
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

    exit_code = EXIT_SUCCESS;
    if (authoring_shell_ready) morph_authoring_shell_destroy(&authoring_shell);
    morph_authoring_controller_shutdown(&authoring_controller);

shutdown_runtime:
    morph_image_service_destroy(image_service);
    morph_http_service_destroy(http_service);
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
