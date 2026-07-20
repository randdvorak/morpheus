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
#define NK_INCLUDE_COMMAND_USERDATA
#define NK_UINT_DRAW_INDEX
#define NK_IMPLEMENTATION
#include "nuklear.h"

_Static_assert(sizeof(nk_draw_index) == 4,
    "Morpheus requires 32-bit Nuklear draw indices");

#define NK_METAL_IMPLEMENTATION
#include "nuklear_metal.h"

#include "morpheus/authoring.h"
#include "authoring_capabilities.h"
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

static int checkpoint_active_module(
    const morph_authoring_revisions_api *revisions_api,
    void *revisions_context,
    const morph_authoring_modules_api *modules_api,
    void *modules_context,
    morph_host *host,
    const char *source_path,
    char *error,
    unsigned long error_capacity)
{
    const void *state_data;
    unsigned long state_size;

    if (!modules_api->capture_state(
            modules_context,
            host,
            &state_data,
            &state_size,
            error,
            error_capacity)) {
        return 0;
    }
    return revisions_api->checkpoint(
        revisions_context,
        source_path,
        state_data,
        state_size,
        NULL,
        error,
        error_capacity);
}

static void record_reload_attempt(
    const morph_authoring_revisions_api *revisions_api,
    void *revisions_context,
    const morph_authoring_modules_api *modules_api,
    void *modules_context,
    int succeeded,
    const char *message)
{
    char error[512];
    if (!revisions_api->record_attempt(
            revisions_context,
            modules_api->stage_name(
                modules_context,
                modules_api->last_stage(modules_context)),
            succeeded,
            message,
            error,
            sizeof(error))) {
        SDL_Log("Unable to record reload attempt: %s", error);
    }
}

static int copy_runtime_state(
    const morph_authoring_modules_api *modules_api,
    void *modules_context,
    morph_host *host,
    void **state_copy,
    unsigned long *state_size,
    char *error,
    unsigned long error_capacity)
{
    const void *state_data = NULL;

    *state_copy = NULL;
    *state_size = 0;
    if (!modules_api->capture_state(
            modules_context,
            host,
            &state_data,
            state_size,
            error,
            error_capacity)) {
        return 0;
    }
    if (*state_size) {
        *state_copy = malloc((size_t)*state_size);
        if (!*state_copy) {
            snprintf(error, (size_t)error_capacity, "Unable to preserve preview rollback state");
            return 0;
        }
        memcpy(*state_copy, state_data, (size_t)*state_size);
    }
    return 1;
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
    morph_runtime_module module;
    morph_revision_store revisions;
    morph_project_store projects;
    morph_capability authoring_entries[5] = {{0}};
    morph_capability_registry authoring_capabilities = {0};
    morph_host authoring_host = {0};
    const morph_capability *projects_provider = NULL;
    const morph_authoring_projects_api *projects_api = NULL;
    void *projects_context = NULL;
    const morph_capability *revisions_provider = NULL;
    const morph_authoring_revisions_api *revisions_api = NULL;
    void *revisions_context = NULL;
    const morph_capability *modules_provider = NULL;
    const morph_authoring_modules_api *modules_api = NULL;
    void *modules_context = NULL;
    const morph_capability *agent_provider = NULL;
    const morph_authoring_agent_api *agent_api = NULL;
    void *agent_context = NULL;
    const morph_capability *export_provider = NULL;
    const morph_authoring_export_api *export_api = NULL;
    void *export_context = NULL;
    unsigned long authoring_capability_count = 0;
    morph_agent_session agent_session;
    morph_export_service export_service;
    morph_http_service *http_service = NULL;
    morph_image_service *image_service = NULL;
    char module_error[4096] = {0};
    char store_error[4096] = {0};
    char revision_label[128];
    char agent_request[MORPHEUS_AUTHORING_AGENT_REQUEST_CAPACITY] = {0};
    char ollama_model[MORPHEUS_AUTHORING_AGENT_MODEL_CAPACITY] = {0};
    char agent_status[256] = "Agent ready";
    char agent_root[MORPHEUS_AUTHORING_AGENT_PATH_CAPACITY];
    char module_source[MORPH_PROJECT_PATH_CAPACITY];
    char workspace_root[MORPH_PROJECT_PATH_CAPACITY];
    char assets_root[MORPH_PROJECT_PATH_CAPACITY];
    char new_project_name[MORPH_PROJECT_NAME_CAPACITY] = {0};
    char projects_root[MORPH_PROJECT_PATH_CAPACITY];
    const char *agent_provider_path;
    const char *agent_backend;
    const char *configured_ollama_model;
    const void *pixels;
    int atlas_width;
    int atlas_height;
    int running = 1;
    int reload_requested = 0;
    int rollback_requested = 0;
    int agent_request_length = 0;
    int ollama_model_length = 0;
    int new_project_name_length = 0;
    int project_switch_requested = -1;
    int project_create_requested = 0;
    int projects_enabled = 0;
    int agent_submit_requested = 0;
    int agent_accept_requested = 0;
    int agent_reject_requested = 0;
    int agent_toggle_provider_requested = 0;
    int agent_ready = 0;
    int agent_preview_active = 0;
    int agent_uses_ollama = 0;
    int agent_provider_is_custom = 0;
    int revision_store_ready = 0;
    int recovered_from_crash = 0;
    void *preview_restore_state = NULL;
    unsigned long preview_restore_state_size = 0;
    int exit_code = EXIT_FAILURE;
    float font_scale;
    Uint64 previous_ticks;

    (void)argc;
    (void)argv;

    const char *module_override = getenv("MORPHEUS_MODULE_SOURCE");
    const char *workspace_override = getenv("MORPHEUS_WORKSPACE_ROOT");
    if (module_override && *module_override && workspace_override && *workspace_override) {
        snprintf(module_source, sizeof(module_source), "%s", module_override);
        snprintf(workspace_root, sizeof(workspace_root), "%s", workspace_override);
        snprintf(assets_root, sizeof(assets_root), "%s/assets", workspace_root);
    } else {
        const char *root_override = getenv("MORPHEUS_PROJECTS_ROOT");
        snprintf(projects_root, sizeof(projects_root), "%s",
            root_override && *root_override ? root_override : MORPHEUS_SOURCE_ROOT "/projects");
        authoring_entries[authoring_capability_count++] =
            morph_authoring_projects_capability(&projects);
        authoring_capabilities.entries = authoring_entries;
        authoring_capabilities.count = authoring_capability_count;
        authoring_host.abi_version = MORPHEUS_HOST_ABI_VERSION;
        authoring_host.capabilities = &authoring_capabilities;
        projects_provider = morph_host_find_capability(
            &authoring_host,
            MORPHEUS_AUTHORING_PROJECTS_CAPABILITY,
            MORPHEUS_AUTHORING_PROJECTS_ABI_VERSION);
        projects_api = morph_authoring_projects_from_capability(
            projects_provider);
        projects_context = projects_provider ? projects_provider->context : NULL;
        projects_enabled = projects_api && projects_api->init(
            projects_context, projects_root, store_error, sizeof(store_error));
        if (!projects_enabled || !projects_api || !projects_api->paths(
                projects_context,
                workspace_root, sizeof(workspace_root),
                module_source, sizeof(module_source),
                assets_root, sizeof(assets_root))) {
            fprintf(stderr, "Unable to initialize projects: %s\n", store_error);
            return EXIT_FAILURE;
        }
    }
    authoring_entries[authoring_capability_count++] =
        morph_authoring_revisions_capability(&revisions);
    authoring_capabilities.entries = authoring_entries;
    authoring_capabilities.count = authoring_capability_count;
    authoring_host.abi_version = MORPHEUS_HOST_ABI_VERSION;
    authoring_host.capabilities = &authoring_capabilities;
    revisions_provider = morph_host_find_capability(
        &authoring_host,
        MORPHEUS_AUTHORING_REVISIONS_CAPABILITY,
        MORPHEUS_AUTHORING_REVISIONS_ABI_VERSION);
    revisions_api = morph_authoring_revisions_from_capability(revisions_provider);
    revisions_context = revisions_provider ? revisions_provider->context : NULL;
    authoring_entries[authoring_capability_count++] =
        morph_authoring_modules_capability(&module);
    authoring_capabilities.count = authoring_capability_count;
    modules_provider = morph_host_find_capability(
        &authoring_host,
        MORPHEUS_AUTHORING_MODULES_CAPABILITY,
        MORPHEUS_AUTHORING_MODULES_ABI_VERSION);
    modules_api = morph_authoring_modules_from_capability(modules_provider);
    modules_context = modules_provider ? modules_provider->context : NULL;
    authoring_entries[authoring_capability_count++] =
        morph_authoring_agent_capability(&agent_session);
    authoring_capabilities.count = authoring_capability_count;
    agent_provider = morph_host_find_capability(
        &authoring_host,
        MORPHEUS_AUTHORING_AGENT_CAPABILITY,
        MORPHEUS_AUTHORING_AGENT_ABI_VERSION);
    agent_api = morph_authoring_agent_from_capability(agent_provider);
    agent_context = agent_provider ? agent_provider->context : NULL;
    authoring_entries[authoring_capability_count++] =
        morph_authoring_export_capability(&export_service, NULL);
    authoring_capabilities.count = authoring_capability_count;
    export_provider = morph_host_find_capability(
        &authoring_host,
        MORPHEUS_AUTHORING_EXPORT_CAPABILITY,
        MORPHEUS_AUTHORING_EXPORT_ABI_VERSION);
    export_api = morph_authoring_export_from_capability(export_provider);
    export_context = export_provider ? export_provider->context : NULL;
    if (!revisions_api || !modules_api || !agent_api || !export_api) {
        fprintf(stderr, "Unable to initialize authoring capabilities\n");
        return EXIT_FAILURE;
    }
    agent_provider_path = getenv("MORPHEUS_AGENT_PROVIDER");
    agent_provider_is_custom = agent_provider_path && *agent_provider_path;
    agent_backend = getenv("MORPHEUS_AGENT_BACKEND");
    if (!agent_provider_is_custom) {
        agent_uses_ollama = agent_backend && strcmp(agent_backend, "ollama") == 0;
        agent_provider_path = agent_uses_ollama
            ? MORPHEUS_OLLAMA_PROVIDER_PATH
            : MORPHEUS_AGENT_PROVIDER_PATH;
    }
    configured_ollama_model = getenv("MORPHEUS_OLLAMA_MODEL");
    if (configured_ollama_model && *configured_ollama_model) {
        ollama_model_length = snprintf(
            ollama_model,
            sizeof(ollama_model),
            "%s",
            configured_ollama_model);
        if (ollama_model_length < 0 ||
            (unsigned long)ollama_model_length >= sizeof(ollama_model)) {
            ollama_model_length = 0;
            ollama_model[0] = '\0';
        }
    }
    snprintf(agent_root, sizeof(agent_root), "%s/agent", workspace_root);

    modules_api->init(modules_context);
    agent_api->reset(agent_context);

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
    morph_apply_light_theme(&ctx);
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
    host.nuklear = &ctx;
    http_service = morph_http_service_create();
    host.http = http_service;
    if (!http_service) {
        SDL_Log("HTTP service unavailable; generated app networking disabled");
    }
    image_service = morph_image_service_create(
        (__bridge void *)metal.device,
        &ctx,
        http_service);
    host.images = image_service;
    if (!image_service) {
        SDL_Log("Image service unavailable; generated app images disabled");
    }

    revision_store_ready = revisions_api->init(
        revisions_context,
        workspace_root,
        store_error,
        sizeof(store_error));
    if (revision_store_ready) {
        revision_store_ready = revisions_api->begin_session(
            revisions_context,
            &recovered_from_crash,
            store_error,
            sizeof(store_error));
    }
    if (!revision_store_ready) {
        SDL_Log("Revision store unavailable: %s", store_error);
    } else if (recovered_from_crash) {
        SDL_Log(
            "Previous generated application exited abnormally; rolled back to revision %lu",
            revisions_api->active_revision(revisions_context));
        (void)revisions_api->record_attempt(
            revisions_context,
            "startup",
            0,
            "abnormal exit detected; automatic rollback",
            store_error,
            sizeof(store_error));
    }

    agent_ready = revision_store_ready && agent_api->init(
            agent_context,
            agent_root,
            agent_provider_path,
            store_error,
            sizeof(store_error));
    if (!agent_ready) {
        snprintf(
            agent_status,
            sizeof(agent_status),
            "Agent unavailable: %.220s",
            revision_store_ready ? store_error : "revision store is required");
    } else {
        (void)agent_api->set_model(
            agent_context,
            agent_uses_ollama ? ollama_model : "",
            store_error,
            sizeof(store_error));
    }

    if (revision_store_ready && revisions_api->active_revision(revisions_context)) {
        char accepted_source[MORPH_REVISION_PATH_CAPACITY];
        void *accepted_state = NULL;
        unsigned long accepted_state_size = 0;
        int accepted_loaded = revisions_api->load(
                revisions_context,
                revisions_api->active_revision(revisions_context),
                accepted_source,
                sizeof(accepted_source),
                &accepted_state,
                &accepted_state_size,
                module_error,
                sizeof(module_error)) &&
            modules_api->compile_candidate(
                modules_context,
                accepted_source,
                module_error,
                sizeof(module_error)) &&
            modules_api->activate_candidate_with_state(
                modules_context,
                &host,
                accepted_state,
                accepted_state_size,
                module_error,
                sizeof(module_error));
        revisions_api->release_state(revisions_context, accepted_state);
        record_reload_attempt(
            revisions_api, revisions_context,
            modules_api, modules_context, accepted_loaded, module_error);
        if (!accepted_loaded) {
            SDL_Log("Accepted revision failed; entering safe mode: %s", module_error);
            (void)revisions_api->set_active(
                revisions_context,
                0,
                store_error,
                sizeof(store_error));
            (void)revisions_api->refresh_session(
                revisions_context,
                store_error,
                sizeof(store_error));
        }
    }

    if (!modules_api->is_active(modules_context)) {
        int initial_loaded = modules_api->reload(
            modules_context,
            &host,
            module_source,
            module_error,
            sizeof(module_error));
        if (revision_store_ready) {
            record_reload_attempt(
                revisions_api, revisions_context,
                modules_api, modules_context, initial_loaded, module_error);
        }
        if (!initial_loaded) {
            SDL_Log("Initial module failed: %s", module_error);
        } else if (revision_store_ready &&
            !checkpoint_active_module(
                revisions_api,
                revisions_context,
                modules_api,
                modules_context,
                &host,
                module_source,
                module_error,
                sizeof(module_error))) {
            SDL_Log("Initial checkpoint failed: %s", module_error);
        } else if (revision_store_ready &&
            !revisions_api->refresh_session(
                revisions_context,
                store_error,
                sizeof(store_error))) {
            SDL_Log("Unable to arm crash recovery: %s", store_error);
        }
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

        morph_http_service_tick(http_service);
        morph_image_service_tick(image_service);
        modules_api->update(modules_context, &host, dt);

        if (nk_begin(&ctx,
                "Morpheus Host",
                nk_rect(30, 30, 430, 650),
                NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE |
                NK_WINDOW_MINIMIZABLE | NK_WINDOW_TITLE)) {
            nk_layout_row_dynamic(&ctx, 28.0f, 1);
            nk_label(&ctx, "Native host: SDL3 + Metal + Nuklear", NK_TEXT_LEFT);
            if (projects_enabled && projects_api) {
                morph_authoring_project_info
                    project_info[MORPHEUS_AUTHORING_MAX_PROJECTS];
                const char *project_names[MORPHEUS_AUTHORING_MAX_PROJECTS];
                unsigned int project_count = projects_api->count(projects_context);
                unsigned int active_index = projects_api->active_index(projects_context);
                unsigned int project_index;
                int selected;
                if (project_count > MORPHEUS_AUTHORING_MAX_PROJECTS) {
                    project_count = MORPHEUS_AUTHORING_MAX_PROJECTS;
                }
                for (project_index = 0; project_index < project_count; ++project_index) {
                    if (!projects_api->project(
                            projects_context, project_index, &project_info[project_index])) {
                        project_count = project_index;
                        break;
                    }
                    project_names[project_index] = project_info[project_index].name;
                }
                nk_layout_row_dynamic(&ctx, 28.0f, 1);
                selected = nk_combo(
                    &ctx,
                    project_names,
                    (int)project_count,
                    (int)active_index,
                    28,
                    nk_vec2(300.0f, 220.0f));
                if (selected != (int)active_index &&
                    !agent_preview_active &&
                    agent_api->status(agent_context) != MORPHEUS_AUTHORING_AGENT_RUNNING) {
                    project_switch_requested = selected;
                }
                nk_layout_row_dynamic(&ctx, 30.0f, 2);
                nk_edit_string(
                    &ctx,
                    NK_EDIT_FIELD,
                    new_project_name,
                    &new_project_name_length,
                    (int)sizeof(new_project_name) - 1,
                    nk_filter_default);
                new_project_name[new_project_name_length] = '\0';
                if (nk_button_label(&ctx, "New App") && new_project_name_length > 0 &&
                    !agent_preview_active &&
                    agent_api->status(agent_context) != MORPHEUS_AUTHORING_AGENT_RUNNING) {
                    project_create_requested = 1;
                }
            }
            nk_label(&ctx,
                modules_api->is_active(modules_context)
                    ? "Runtime C module: active"
                    : "Runtime C module: unavailable",
                NK_TEXT_LEFT);
            if (modules_api->active_name(modules_context)) {
                nk_label(&ctx, modules_api->active_name(modules_context), NK_TEXT_LEFT);
            }
            if (revision_store_ready) {
                snprintf(
                    revision_label,
                    sizeof(revision_label),
                    "Accepted revision: %lu",
                    revisions_api->active_revision(revisions_context));
                nk_label(&ctx, revision_label, NK_TEXT_LEFT);
                if (recovered_from_crash) {
                    nk_label(
                        &ctx,
                        "Safe mode: previous revision exited abnormally",
                        NK_TEXT_LEFT);
                }
            }

            nk_layout_row_dynamic(&ctx, 34.0f, 1);
            if (!agent_preview_active &&
                agent_api->status(agent_context) != MORPHEUS_AUTHORING_AGENT_RUNNING &&
                nk_button_label(&ctx, "Recompile current app")) {
                reload_requested = 1;
            }
            if (!agent_preview_active &&
                agent_api->status(agent_context) != MORPHEUS_AUTHORING_AGENT_RUNNING &&
                revision_store_ready &&
                revisions_api->active_revision(revisions_context) > 1) {
                nk_layout_row_dynamic(&ctx, 34.0f, 1);
                if (nk_button_label(&ctx, "Rollback previous revision")) {
                    rollback_requested = 1;
                }
            }

            nk_layout_row_dynamic(&ctx, 24.0f, 1);
            nk_label(&ctx, "Agent policy: isolated manual preview", NK_TEXT_LEFT);
            nk_layout_row_dynamic(&ctx, 30.0f, 1);
            if (!agent_provider_is_custom && !agent_preview_active &&
                agent_api->status(agent_context) != MORPHEUS_AUTHORING_AGENT_RUNNING) {
                if (nk_button_label(
                        &ctx,
                        agent_uses_ollama
                            ? "Provider: Ollama (switch to Codex)"
                            : "Provider: Codex (switch to Ollama)")) {
                    agent_toggle_provider_requested = 1;
                }
            } else {
                nk_label(
                    &ctx,
                    agent_provider_is_custom
                        ? "Provider: custom executable"
                        : (agent_uses_ollama ? "Provider: Ollama" : "Provider: Codex"),
                    NK_TEXT_LEFT);
            }
            if (agent_uses_ollama && !agent_provider_is_custom) {
                nk_layout_row_dynamic(&ctx, 24.0f, 1);
                nk_label(&ctx, "Ollama model (blank selects first installed):", NK_TEXT_LEFT);
                nk_layout_row_dynamic(&ctx, 30.0f, 1);
                nk_edit_string(
                    &ctx,
                    NK_EDIT_FIELD,
                    ollama_model,
                    &ollama_model_length,
                    (int)sizeof(ollama_model) - 1,
                    nk_filter_default);
                ollama_model[ollama_model_length] = '\0';
            }
            nk_layout_row_dynamic(&ctx, 24.0f, 1);
            nk_label(&ctx, "Describe a generated application change:", NK_TEXT_LEFT);
            nk_layout_row_dynamic(&ctx, 72.0f, 1);
            nk_edit_string(
                &ctx,
                NK_EDIT_BOX,
                agent_request,
                &agent_request_length,
                (int)sizeof(agent_request) - 1,
                nk_filter_default);
            agent_request[agent_request_length] = '\0';
            nk_layout_row_dynamic(&ctx, 34.0f, 1);
            if (agent_ready && !agent_preview_active &&
                agent_api->status(agent_context) != MORPHEUS_AUTHORING_AGENT_RUNNING &&
                agent_request_length > 0 &&
                nk_button_label(&ctx, "Ask coding agent")) {
                agent_submit_requested = 1;
            }
            nk_layout_row_dynamic(&ctx, 24.0f, 1);
            nk_label(&ctx, agent_status, NK_TEXT_LEFT);

            if (agent_preview_active) {
                nk_layout_row_dynamic(&ctx, 34.0f, 2);
                if (nk_button_label(&ctx, "Accept preview")) {
                    agent_accept_requested = 1;
                }
                if (nk_button_label(&ctx, "Reject preview")) {
                    agent_reject_requested = 1;
                }
            }

            if (module_error[0]) {
                nk_layout_row_dynamic(&ctx, 90.0f, 1);
                nk_label_wrap(&ctx, module_error);
            }
        }
        nk_end(&ctx);

        if (agent_accept_requested || agent_reject_requested) {
            /* Keep preview decisions responsive even when generated rendering
               is expensive. The host controls are still drawn and cleared. */
        } else if (modules_api->render_mode(modules_context) ==
                MORPHEUS_RENDER_NUKLEAR_WINDOWS) {
            modules_api->render_ui(modules_context, &host);
        } else {
            if (nk_begin(&ctx,
                    "Generated Application",
                    nk_rect(500, 30, 520, 300),
                    NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE |
                    NK_WINDOW_MINIMIZABLE | NK_WINDOW_TITLE)) {
                modules_api->render_ui(modules_context, &host);
            }
            nk_end(&ctx);
        }

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

        if (project_create_requested) {
            project_create_requested = 0;
            if (projects_api && projects_api->create(
                    projects_context,
                    new_project_name,
                    module_error,
                    sizeof(module_error))) {
                project_switch_requested =
                    (int)projects_api->active_index(projects_context);
                new_project_name_length = 0;
                new_project_name[0] = '\0';
            }
        }

        if (project_switch_requested >= 0) {
            char accepted_source[MORPH_REVISION_PATH_CAPACITY];
            void *accepted_state = NULL;
            unsigned long accepted_state_size = 0;
            int selected_loaded = 0;
            unsigned int selected_index = (unsigned int)project_switch_requested;
            project_switch_requested = -1;

            if (modules_api->is_active(modules_context) && revision_store_ready) {
                (void)checkpoint_active_module(
                    revisions_api, revisions_context,
                    modules_api, modules_context,
                    &host, module_source,
                    store_error, sizeof(store_error));
                (void)revisions_api->end_session(
                    revisions_context, store_error, sizeof(store_error));
            }
            modules_api->destroy(modules_context, &host);
            modules_api->init(modules_context);
            agent_api->cancel(agent_context);
            agent_api->reset(agent_context);
            free(preview_restore_state);
            preview_restore_state = NULL;
            preview_restore_state_size = 0;
            agent_preview_active = 0;
            recovered_from_crash = 0;

            if (!projects_api || !projects_api->select(
                    projects_context,
                    selected_index,
                    module_error,
                    sizeof(module_error)) ||
                !projects_api->paths(
                    projects_context,
                    workspace_root, sizeof(workspace_root),
                    module_source, sizeof(module_source),
                    assets_root, sizeof(assets_root))) {
                revision_store_ready = 0;
                agent_ready = 0;
            } else {
                snprintf(agent_root, sizeof(agent_root), "%s/agent", workspace_root);
                revision_store_ready = revisions_api->init(
                    revisions_context, workspace_root, store_error, sizeof(store_error));
                if (revision_store_ready) {
                    revision_store_ready = revisions_api->begin_session(
                        revisions_context, &recovered_from_crash,
                        store_error, sizeof(store_error));
                }
                agent_ready = revision_store_ready && agent_api->init(
                    agent_context, agent_root, agent_provider_path,
                    store_error, sizeof(store_error));
                if (agent_ready) {
                    agent_ready = agent_api->set_model(
                        agent_context,
                        agent_uses_ollama ? ollama_model : "",
                        store_error,
                        sizeof(store_error));
                }
                if (revision_store_ready &&
                    revisions_api->active_revision(revisions_context)) {
                    selected_loaded = revisions_api->load(
                            revisions_context,
                            revisions_api->active_revision(revisions_context),
                            accepted_source,
                            sizeof(accepted_source),
                            &accepted_state,
                            &accepted_state_size,
                            module_error,
                            sizeof(module_error)) &&
                        modules_api->compile_candidate(
                            modules_context, accepted_source,
                            module_error, sizeof(module_error)) &&
                        modules_api->activate_candidate_with_state(
                            modules_context, &host,
                            accepted_state, accepted_state_size,
                            module_error, sizeof(module_error));
                    revisions_api->release_state(revisions_context, accepted_state);
                }
                if (!selected_loaded) {
                    selected_loaded = modules_api->reload(
                        modules_context, &host, module_source,
                        module_error, sizeof(module_error));
                    if (selected_loaded && revision_store_ready) {
                        selected_loaded = checkpoint_active_module(
                                revisions_api, revisions_context,
                                modules_api, modules_context,
                                &host, module_source,
                                module_error, sizeof(module_error)) &&
                            revisions_api->refresh_session(
                                revisions_context, module_error, sizeof(module_error));
                    }
                }
                snprintf(
                    agent_status,
                    sizeof(agent_status),
                    selected_loaded ? "Project ready" : "Project failed to load");
                if (selected_loaded) module_error[0] = '\0';
            }
        } else if (agent_toggle_provider_requested) {
            agent_toggle_provider_requested = 0;
            agent_uses_ollama = !agent_uses_ollama;
            agent_provider_path = agent_uses_ollama
                ? MORPHEUS_OLLAMA_PROVIDER_PATH
                : MORPHEUS_AGENT_PROVIDER_PATH;
            agent_ready = agent_api->init(
                agent_context,
                agent_root,
                agent_provider_path,
                module_error,
                sizeof(module_error));
            if (agent_ready) {
                agent_ready = agent_api->set_model(
                    agent_context,
                    agent_uses_ollama ? ollama_model : "",
                    module_error,
                    sizeof(module_error));
            }
            if (agent_ready) {
                snprintf(
                    agent_status,
                    sizeof(agent_status),
                    "%s provider ready",
                    agent_uses_ollama ? "Ollama" : "Codex");
                module_error[0] = '\0';
            } else {
                snprintf(
                    agent_status,
                    sizeof(agent_status),
                    "%s provider unavailable",
                    agent_uses_ollama ? "Ollama" : "Codex");
            }
        } else if (agent_accept_requested) {
            int accepted = 0;
            int source_updated = 0;
            agent_accept_requested = 0;
            if (agent_preview_active && revision_store_ready) {
                source_updated = agent_api->accept_source(
                        agent_context,
                        module_source,
                        module_error,
                        sizeof(module_error));
                accepted = source_updated && checkpoint_active_module(
                    revisions_api,
                    revisions_context,
                    modules_api,
                    modules_context,
                    &host,
                    agent_api->candidate_path(agent_context),
                    module_error,
                    sizeof(module_error));
                if (source_updated && !accepted) {
                    (void)agent_api->restore_source(
                        agent_context,
                        module_source,
                        module_error,
                        sizeof(module_error));
                }
            }
            if (accepted) {
                if (!revisions_api->refresh_session(
                        revisions_context,
                        store_error,
                        sizeof(store_error))) {
                    SDL_Log("Unable to arm accepted revision crash recovery: %s", store_error);
                }
                (void)agent_api->record_outcome(
                    agent_context,
                    "accepted",
                    revisions_api->active_revision(revisions_context),
                    store_error,
                    sizeof(store_error));
                free(preview_restore_state);
                preview_restore_state = NULL;
                preview_restore_state_size = 0;
                agent_preview_active = 0;
                agent_request_length = 0;
                agent_request[0] = '\0';
                snprintf(
                    agent_status,
                    sizeof(agent_status),
                    "Accepted as revision %lu",
                    revisions_api->active_revision(revisions_context));
                module_error[0] = '\0';
            }
        } else if (agent_reject_requested) {
            int rejected;
            agent_reject_requested = 0;
            rejected = agent_preview_active &&
                modules_api->compile_candidate(
                    modules_context,
                    agent_api->source_before_path(agent_context),
                    module_error,
                    sizeof(module_error)) &&
                modules_api->activate_candidate_with_state(
                    modules_context,
                    &host,
                    preview_restore_state,
                    preview_restore_state_size,
                    module_error,
                    sizeof(module_error));
            if (rejected) {
                (void)revisions_api->refresh_session(
                    revisions_context,
                    store_error,
                    sizeof(store_error));
                (void)agent_api->record_outcome(
                    agent_context,
                    "rejected",
                    revisions_api->active_revision(revisions_context),
                    store_error,
                    sizeof(store_error));
                free(preview_restore_state);
                preview_restore_state = NULL;
                preview_restore_state_size = 0;
                agent_preview_active = 0;
                snprintf(agent_status, sizeof(agent_status), "Preview rejected; accepted revision restored");
                module_error[0] = '\0';
            }
        } else if (agent_submit_requested) {
            char accepted_source[MORPH_REVISION_PATH_CAPACITY];
            void *accepted_state = NULL;
            unsigned long accepted_state_size = 0;
            const char *agent_base_source = module_source;
            int base_ready = 1;

            agent_submit_requested = 0;
            if (revisions_api->active_revision(revisions_context)) {
                base_ready = revisions_api->load(
                    revisions_context,
                    revisions_api->active_revision(revisions_context),
                    accepted_source,
                    sizeof(accepted_source),
                    &accepted_state,
                    &accepted_state_size,
                    module_error,
                    sizeof(module_error));
                agent_base_source = accepted_source;
            }
            revisions_api->release_state(revisions_context, accepted_state);
            if (base_ready && agent_api->set_model(
                    agent_context,
                    agent_uses_ollama ? ollama_model : "",
                    module_error,
                    sizeof(module_error)) &&
                agent_api->begin(
                    agent_context,
                    agent_request,
                    agent_base_source,
                    MORPHEUS_SOURCE_ROOT "/include/morpheus/app_api.h",
                    MORPHEUS_SOURCE_ROOT "/include/morpheus/sdk.h",
                    module_error,
                    sizeof(module_error)) &&
                agent_api->start_attempt(
                    agent_context,
                    "",
                    module_error,
                    sizeof(module_error))) {
                snprintf(
                    agent_status,
                    sizeof(agent_status),
                    "Agent attempt 1 of %d running",
                    MORPHEUS_AUTHORING_AGENT_MAX_ATTEMPTS);
                module_error[0] = '\0';
            }
        } else if (rollback_requested) {
            char rollback_source[MORPH_REVISION_PATH_CAPACITY];
            void *rollback_state = NULL;
            unsigned long rollback_state_size = 0;
            unsigned long rollback_revision = 0;
            int rollback_succeeded;

            rollback_requested = 0;
            rollback_succeeded =
                revisions_api->previous(revisions_context, &rollback_revision) &&
                revisions_api->load(
                    revisions_context,
                    rollback_revision,
                    rollback_source,
                    sizeof(rollback_source),
                    &rollback_state,
                    &rollback_state_size,
                    module_error,
                    sizeof(module_error)) &&
                modules_api->compile_candidate(
                    modules_context,
                    rollback_source,
                    module_error,
                    sizeof(module_error)) &&
                modules_api->activate_candidate_with_state(
                    modules_context,
                    &host,
                    rollback_state,
                    rollback_state_size,
                    module_error,
                    sizeof(module_error)) &&
                revisions_api->set_active(
                    revisions_context,
                    rollback_revision,
                    module_error,
                    sizeof(module_error)) &&
                revisions_api->refresh_session(
                    revisions_context,
                    module_error,
                    sizeof(module_error));
            revisions_api->release_state(revisions_context, rollback_state);
            record_reload_attempt(
                revisions_api,
                revisions_context,
                modules_api,
                modules_context,
                rollback_succeeded,
                module_error);
            if (rollback_succeeded) {
                module_error[0] = '\0';
            }
        } else if (reload_requested) {
            int reload_succeeded;
            reload_requested = 0;
            reload_succeeded = modules_api->compile_candidate(
                    modules_context,
                    module_source,
                    module_error,
                    sizeof(module_error)) &&
                modules_api->activate_candidate(
                    modules_context,
                    &host,
                    module_error,
                    sizeof(module_error));
            if (revision_store_ready) {
                record_reload_attempt(
                    revisions_api,
                    revisions_context,
                    modules_api,
                    modules_context,
                    reload_succeeded,
                    module_error);
            }
            if (reload_succeeded && revision_store_ready) {
                reload_succeeded = checkpoint_active_module(
                    revisions_api,
                    revisions_context,
                    modules_api,
                    modules_context,
                    &host,
                    module_source,
                    module_error,
                    sizeof(module_error)) &&
                    revisions_api->refresh_session(
                        revisions_context,
                        module_error,
                        sizeof(module_error));
            }
            if (reload_succeeded) {
                module_error[0] = '\0';
            }
        }

        if (agent_api->status(agent_context) == MORPHEUS_AUTHORING_AGENT_RUNNING) {
            int agent_finished = 0;
            if (!agent_api->poll(
                    agent_context,
                    &agent_finished,
                    module_error,
                    sizeof(module_error))) {
                snprintf(agent_status, sizeof(agent_status), "Agent process monitoring failed");
                (void)agent_api->record_outcome(
                    agent_context,
                    "failed",
                    revisions_api->active_revision(revisions_context),
                    store_error,
                    sizeof(store_error));
            } else if (agent_finished) {
                int candidate_succeeded = 0;
                if (agent_api->status(agent_context) ==
                    MORPHEUS_AUTHORING_AGENT_PROVIDER_SUCCEEDED) {
                    candidate_succeeded = agent_api->candidate_changed(
                            agent_context,
                            module_error,
                            sizeof(module_error)) &&
                        modules_api->compile_candidate(
                            modules_context,
                            agent_api->candidate_path(agent_context),
                            module_error,
                            sizeof(module_error)) &&
                        copy_runtime_state(
                            modules_api,
                            modules_context,
                            &host,
                            &preview_restore_state,
                            &preview_restore_state_size,
                            module_error,
                            sizeof(module_error));
                    if (candidate_succeeded && revision_store_ready) {
                        candidate_succeeded = revisions_api->end_session(
                            revisions_context,
                            store_error,
                            sizeof(store_error));
                    }
                    if (candidate_succeeded) {
                        candidate_succeeded = modules_api->activate_candidate(
                            modules_context,
                            &host,
                            module_error,
                            sizeof(module_error));
                    }
                    (void)agent_api->record_build(
                        agent_context,
                        candidate_succeeded,
                        modules_api->stage_name(
                            modules_context,
                            modules_api->last_stage(modules_context)),
                        module_error,
                        store_error,
                        sizeof(store_error));
                    (void)agent_api->create_patch(
                        agent_context,
                        store_error,
                        sizeof(store_error));
                } else {
                    if (!agent_api->read_provider_log(
                            agent_context,
                            module_error,
                            sizeof(module_error))) {
                        snprintf(module_error, sizeof(module_error), "External coding agent exited unsuccessfully");
                    }
                }

                if (candidate_succeeded) {
                    agent_preview_active = 1;
                    snprintf(agent_status, sizeof(agent_status), "Preview ready — accept or reject");
                    module_error[0] = '\0';
                } else {
                    free(preview_restore_state);
                    preview_restore_state = NULL;
                    preview_restore_state_size = 0;
                    if (revision_store_ready) {
                        (void)revisions_api->refresh_session(
                            revisions_context,
                            store_error,
                            sizeof(store_error));
                    }
                    if (agent_api->status(agent_context) ==
                            MORPHEUS_AUTHORING_AGENT_PROVIDER_SUCCEEDED &&
                        agent_api->attempt(agent_context) <
                            MORPHEUS_AUTHORING_AGENT_MAX_ATTEMPTS &&
                        agent_api->start_attempt(
                            agent_context,
                            module_error,
                            store_error,
                            sizeof(store_error))) {
                        snprintf(
                            agent_status,
                            sizeof(agent_status),
                            "Diagnostics returned; repair attempt %u of %d running",
                            agent_api->attempt(agent_context),
                            MORPHEUS_AUTHORING_AGENT_MAX_ATTEMPTS);
                        module_error[0] = '\0';
                    } else {
                        (void)agent_api->record_outcome(
                            agent_context,
                            "failed",
                            revisions_api->active_revision(revisions_context),
                            store_error,
                            sizeof(store_error));
                        snprintf(
                            agent_status,
                            sizeof(agent_status),
                            "Agent run failed after %u attempt(s)",
                            agent_api->attempt(agent_context));
                    }
                }
            }
        }
    }

    exit_code = EXIT_SUCCESS;
    modules_api->destroy(modules_context, &host);
    morph_image_service_destroy(image_service);
    morph_http_service_destroy(http_service);
    http_service = NULL;
    agent_api->cancel(agent_context);
    export_api->cancel(export_context);
    free(preview_restore_state);
    if (revision_store_ready &&
        !revisions_api->end_session(
            revisions_context,
            store_error,
            sizeof(store_error))) {
        SDL_Log("Unable to close revision session: %s", store_error);
    }
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
