#include "runtime_module.h"

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#include "libtcc.h"

#ifdef MORPHEUS_ENABLE_RUNTIME_LEAKCHECK
#include "runtime_leakcheck.h"
#endif

typedef struct morph_error_sink {
    char *buffer;
    unsigned long capacity;
    unsigned long length;
} morph_error_sink;

static void morph_tcc_error(void *opaque, const char *message)
{
    morph_error_sink *sink = (morph_error_sink *)opaque;
    unsigned long available;
    int written;

    if (!sink || !sink->buffer || sink->capacity == 0) {
        return;
    }

    available = sink->capacity - sink->length;
    if (available <= 1) {
        return;
    }

    written = snprintf(
        sink->buffer + sink->length,
        (size_t)available,
        "%s%s",
        sink->length ? "\n" : "",
        message);
    if (written < 0) {
        return;
    }
    if ((unsigned long)written >= available) {
        sink->length = sink->capacity - 1;
    } else {
        sink->length += (unsigned long)written;
    }
}
void morph_runtime_module_init(morph_runtime_module *module)
{
    memset(module, 0, sizeof(*module));
}

static void morph_release_candidate(
    TCCState *compiler,
    const morph_app_api *api,
    morph_host *host,
    void *state)
{
    if (api && api->destroy) {
        api->destroy(host, state);
    }
    if (compiler) {
        tcc_delete(compiler);
    }
}

static void morph_set_error(
    char *error,
    unsigned long error_capacity,
    const char *message)
{
    if (error && error_capacity) {
        snprintf(error, (size_t)error_capacity, "%s", message);
    }
}

static int morph_fail(
    morph_runtime_module *module,
    morph_runtime_stage stage,
    char *error,
    unsigned long error_capacity,
    const char *message)
{
    module->last_stage = stage;
    morph_set_error(error, error_capacity, message);
    return 0;
}

static void morph_discard_pending(morph_runtime_module *module)
{
    if (module->pending_compiler) {
        tcc_delete((TCCState *)module->pending_compiler);
    }
    module->pending_compiler = NULL;
    module->pending_api = NULL;
    module->pending_render_mode = MORPHEUS_RENDER_EMBEDDED;
}

int morph_runtime_module_stage_static_candidate(
    morph_runtime_module *module,
    const morph_app_api *api,
    unsigned int render_mode,
    char *error,
    unsigned long error_capacity)
{
    if (error && error_capacity) error[0] = '\0';
    if (!module || !api || api->abi_version != MORPHEUS_APP_ABI_VERSION ||
        !api->render_ui || render_mode > MORPHEUS_RENDER_NUKLEAR_WINDOWS) {
        if (module) module->last_stage = MORPH_RUNTIME_STAGE_VALIDATE;
        morph_set_error(error, error_capacity,
            "Static candidate returned an invalid application ABI");
        return 0;
    }
    morph_discard_pending(module);
    module->pending_api = api;
    module->pending_render_mode = render_mode;
    module->last_stage = MORPH_RUNTIME_STAGE_VALIDATE;
    return 1;
}

static void morph_add_nuklear_symbols(TCCState *compiler)
{
    void *address;
#define MORPHEUS_NUKLEAR_SYMBOL(name) \
    do { \
        address = dlsym(RTLD_DEFAULT, #name); \
        if (address) (void)tcc_add_symbol(compiler, #name, address); \
    } while (0);
#include "morpheus_nuklear_symbols.inc"
#undef MORPHEUS_NUKLEAR_SYMBOL
#define MORPHEUS_RUNTIME_SYMBOL(name) \
    do { \
        address = dlsym(RTLD_DEFAULT, #name); \
        if (address) (void)tcc_add_symbol(compiler, #name, address); \
    } while (0);
    MORPHEUS_RUNTIME_SYMBOL(memcpy)
    MORPHEUS_RUNTIME_SYMBOL(memmove)
    MORPHEUS_RUNTIME_SYMBOL(memset)
    MORPHEUS_RUNTIME_SYMBOL(memcmp)
    MORPHEUS_RUNTIME_SYMBOL(malloc)
    MORPHEUS_RUNTIME_SYMBOL(calloc)
    MORPHEUS_RUNTIME_SYMBOL(realloc)
    MORPHEUS_RUNTIME_SYMBOL(free)
    MORPHEUS_RUNTIME_SYMBOL(strlen)
    MORPHEUS_RUNTIME_SYMBOL(strcmp)
    MORPHEUS_RUNTIME_SYMBOL(strncmp)
    MORPHEUS_RUNTIME_SYMBOL(strncpy)
    MORPHEUS_RUNTIME_SYMBOL(strchr)
    MORPHEUS_RUNTIME_SYMBOL(strrchr)
    MORPHEUS_RUNTIME_SYMBOL(strtol)
    MORPHEUS_RUNTIME_SYMBOL(strtoul)
    MORPHEUS_RUNTIME_SYMBOL(strtod)
    MORPHEUS_RUNTIME_SYMBOL(atoi)
    MORPHEUS_RUNTIME_SYMBOL(atof)
    MORPHEUS_RUNTIME_SYMBOL(snprintf)
    MORPHEUS_RUNTIME_SYMBOL(vsnprintf)
    MORPHEUS_RUNTIME_SYMBOL(qsort)
    MORPHEUS_RUNTIME_SYMBOL(bsearch)
    MORPHEUS_RUNTIME_SYMBOL(morph_http_get)
    MORPHEUS_RUNTIME_SYMBOL(morph_http_post_json)
    MORPHEUS_RUNTIME_SYMBOL(morph_http_poll)
    MORPHEUS_RUNTIME_SYMBOL(morph_http_cancel)
    MORPHEUS_RUNTIME_SYMBOL(morph_image_load_memory)
    MORPHEUS_RUNTIME_SYMBOL(morph_image_load_rgba)
    MORPHEUS_RUNTIME_SYMBOL(morph_image_load_url)
    MORPHEUS_RUNTIME_SYMBOL(morph_image_poll)
    MORPHEUS_RUNTIME_SYMBOL(morph_image_draw)
    MORPHEUS_RUNTIME_SYMBOL(morph_image_release)
    MORPHEUS_RUNTIME_SYMBOL(morph_json_parse)
    MORPHEUS_RUNTIME_SYMBOL(morph_json_document_free)
    MORPHEUS_RUNTIME_SYMBOL(morph_json_root)
    MORPHEUS_RUNTIME_SYMBOL(morph_json_value_type)
    MORPHEUS_RUNTIME_SYMBOL(morph_json_object_get)
    MORPHEUS_RUNTIME_SYMBOL(morph_json_array_size)
    MORPHEUS_RUNTIME_SYMBOL(morph_json_array_get)
    MORPHEUS_RUNTIME_SYMBOL(morph_json_get_boolean)
    MORPHEUS_RUNTIME_SYMBOL(morph_json_get_integer)
    MORPHEUS_RUNTIME_SYMBOL(morph_json_get_number)
    MORPHEUS_RUNTIME_SYMBOL(morph_json_get_string)
    MORPHEUS_RUNTIME_SYMBOL(morph_json_builder_create)
    MORPHEUS_RUNTIME_SYMBOL(morph_json_builder_free)
    MORPHEUS_RUNTIME_SYMBOL(morph_json_make_null)
    MORPHEUS_RUNTIME_SYMBOL(morph_json_make_boolean)
    MORPHEUS_RUNTIME_SYMBOL(morph_json_make_integer)
    MORPHEUS_RUNTIME_SYMBOL(morph_json_make_number)
    MORPHEUS_RUNTIME_SYMBOL(morph_json_make_string)
    MORPHEUS_RUNTIME_SYMBOL(morph_json_make_array)
    MORPHEUS_RUNTIME_SYMBOL(morph_json_make_object)
    MORPHEUS_RUNTIME_SYMBOL(morph_json_array_append)
    MORPHEUS_RUNTIME_SYMBOL(morph_json_object_set)
    MORPHEUS_RUNTIME_SYMBOL(morph_json_builder_set_root)
    MORPHEUS_RUNTIME_SYMBOL(morph_json_serialize)
    MORPHEUS_RUNTIME_SYMBOL(morph_json_buffer_free)
#ifdef MORPHEUS_ENABLE_RUNTIME_LEAKCHECK
    MORPHEUS_RUNTIME_SYMBOL(morph_runtime_leakcheck_malloc)
    MORPHEUS_RUNTIME_SYMBOL(morph_runtime_leakcheck_calloc)
    MORPHEUS_RUNTIME_SYMBOL(morph_runtime_leakcheck_realloc)
    MORPHEUS_RUNTIME_SYMBOL(morph_runtime_leakcheck_free)
#endif
#undef MORPHEUS_RUNTIME_SYMBOL
}

int morph_runtime_module_compile_candidate(
    morph_runtime_module *module,
    const char *source_path,
    char *error,
    unsigned long error_capacity)
{
    TCCState *candidate;
    morph_app_entry_fn entry;
    morph_app_render_mode_fn render_mode_entry;
    const morph_app_api *candidate_api;
    unsigned int candidate_render_mode;
    morph_error_sink sink = {error, error_capacity, 0};

    if (error && error_capacity) {
        error[0] = '\0';
    }
    morph_discard_pending(module);
    module->last_stage = MORPH_RUNTIME_STAGE_COMPILE;

    candidate = tcc_new();
    if (!candidate) {
        return morph_fail(
            module,
            MORPH_RUNTIME_STAGE_COMPILE,
            error,
            error_capacity,
            "Unable to create TinyCC state");
    }

    tcc_set_error_func(candidate, &sink, morph_tcc_error);
    tcc_set_lib_path(candidate, MORPHEUS_TCC_LIB_PATH);
    tcc_set_options(candidate, "-nostdlib -Wall -Werror");
    tcc_add_include_path(candidate, MORPHEUS_SOURCE_ROOT "/include");
    tcc_add_include_path(candidate, MORPHEUS_SOURCE_ROOT "/Nuklear");
    tcc_add_include_path(candidate, MORPHEUS_TCC_INCLUDE_PATH);
#ifdef MORPHEUS_ENABLE_RUNTIME_LEAKCHECK
    tcc_define_symbol(candidate, "MORPHEUS_ENABLE_RUNTIME_LEAKCHECK", "1");
#endif

    if (tcc_set_output_type(candidate, TCC_OUTPUT_MEMORY) < 0) {
        tcc_delete(candidate);
        module->last_stage = MORPH_RUNTIME_STAGE_COMPILE;
        return 0;
    }
    morph_add_nuklear_symbols(candidate);

    if (
        tcc_add_file(candidate, source_path) < 0 ||
        tcc_relocate(candidate) < 0) {
        tcc_delete(candidate);
        module->last_stage = MORPH_RUNTIME_STAGE_COMPILE;
        return 0;
    }

    module->last_stage = MORPH_RUNTIME_STAGE_VALIDATE;
    entry = (morph_app_entry_fn)tcc_get_symbol(candidate, "morph_app_entry");
    if (!entry) {
        morph_set_error(
            error,
            error_capacity,
            "Candidate does not export morph_app_entry");
        tcc_delete(candidate);
        return 0;
    }

    candidate_api = entry();
    render_mode_entry = (morph_app_render_mode_fn)tcc_get_symbol(
        candidate,
        "morph_app_render_mode");
    candidate_render_mode = render_mode_entry
        ? render_mode_entry()
        : MORPHEUS_RENDER_EMBEDDED;
    if (!candidate_api ||
        candidate_api->abi_version != MORPHEUS_APP_ABI_VERSION ||
        !candidate_api->render_ui ||
        candidate_render_mode > MORPHEUS_RENDER_NUKLEAR_WINDOWS) {
        morph_set_error(
            error,
            error_capacity,
            "Candidate returned an invalid application ABI");
        tcc_delete(candidate);
        return 0;
    }

    module->pending_compiler = candidate;
    module->pending_api = candidate_api;
    module->pending_render_mode = candidate_render_mode;
    return 1;
}

int morph_runtime_module_capture_state(
    morph_runtime_module *module,
    morph_host *host,
    const void **state_data,
    unsigned long *state_size,
    char *error,
    unsigned long error_capacity)
{
    if (error && error_capacity) {
        error[0] = '\0';
    }
    if (!state_data || !state_size) {
        return morph_fail(
            module,
            MORPH_RUNTIME_STAGE_SAVE_STATE,
            error,
            error_capacity,
            "State output arguments are required");
    }

    *state_data = NULL;
    *state_size = 0;
    if (module->api && module->api->save_state &&
        !module->api->save_state(
            host,
            module->state,
            state_data,
            state_size)) {
        return morph_fail(
            module,
            MORPH_RUNTIME_STAGE_SAVE_STATE,
            error,
            error_capacity,
            "Active module state save failed");
    }
    if (*state_size && !*state_data) {
        return morph_fail(
            module,
            MORPH_RUNTIME_STAGE_SAVE_STATE,
            error,
            error_capacity,
            "Active module returned invalid state");
    }
    return 1;
}

static int morph_activate_candidate(
    morph_runtime_module *module,
    morph_host *host,
    const void *saved_data,
    unsigned long saved_size,
    int use_explicit_state,
    char *error,
    unsigned long error_capacity)
{
    TCCState *candidate;
    const morph_app_api *candidate_api;
    void *candidate_state = NULL;
    TCCState *previous_compiler;
    const morph_app_api *previous_api;
    void *previous_state;

    if (error && error_capacity) {
        error[0] = '\0';
    }
    if (!module->pending_api) {
        return morph_fail(
            module,
            MORPH_RUNTIME_STAGE_VALIDATE,
            error,
            error_capacity,
            "No compiled candidate is pending");
    }

    candidate = (TCCState *)module->pending_compiler;
    candidate_api = module->pending_api;

    if (!use_explicit_state) {
        module->last_stage = MORPH_RUNTIME_STAGE_SAVE_STATE;
        if (!morph_runtime_module_capture_state(
                module,
                host,
                &saved_data,
                &saved_size,
                error,
                error_capacity)) {
            morph_discard_pending(module);
            return 0;
        }
    } else if (saved_size && !saved_data) {
        morph_discard_pending(module);
        return morph_fail(
            module,
            MORPH_RUNTIME_STAGE_SAVE_STATE,
            error,
            error_capacity,
            "Rollback state is invalid");
    }

    module->last_stage = MORPH_RUNTIME_STAGE_INITIALIZE;
    if (candidate_api->create &&
        !candidate_api->create(host, &candidate_state)) {
        morph_release_candidate(candidate, candidate_api, host, candidate_state);
        module->pending_compiler = NULL;
        module->pending_api = NULL;
        return morph_fail(
            module,
            MORPH_RUNTIME_STAGE_INITIALIZE,
            error,
            error_capacity,
            "Candidate initialization failed");
    }

    module->last_stage = MORPH_RUNTIME_STAGE_MIGRATE;
    if (saved_size &&
        (!candidate_api->load_state ||
         !candidate_api->load_state(
            host,
            &candidate_state,
            saved_data,
            saved_size))) {
        morph_release_candidate(candidate, candidate_api, host, candidate_state);
        module->pending_compiler = NULL;
        module->pending_api = NULL;
        return morph_fail(
            module,
            MORPH_RUNTIME_STAGE_MIGRATE,
            error,
            error_capacity,
            "Candidate state migration failed");
    }

    previous_compiler = (TCCState *)module->compiler;
    previous_api = module->api;
    previous_state = module->state;

    module->compiler = candidate;
    module->api = candidate_api;
    module->state = candidate_state;
    module->render_mode = module->pending_render_mode;
    module->pending_compiler = NULL;
    module->pending_api = NULL;
    module->pending_render_mode = MORPHEUS_RENDER_EMBEDDED;

    morph_release_candidate(
        previous_compiler,
        previous_api,
        host,
        previous_state);
    module->last_stage = MORPH_RUNTIME_STAGE_ACTIVE;
    return 1;
}

int morph_runtime_module_activate_candidate(
    morph_runtime_module *module,
    morph_host *host,
    char *error,
    unsigned long error_capacity)
{
    return morph_activate_candidate(
        module,
        host,
        NULL,
        0,
        0,
        error,
        error_capacity);
}

int morph_runtime_module_activate_candidate_with_state(
    morph_runtime_module *module,
    morph_host *host,
    const void *state_data,
    unsigned long state_size,
    char *error,
    unsigned long error_capacity)
{
    return morph_activate_candidate(
        module,
        host,
        state_data,
        state_size,
        1,
        error,
        error_capacity);
}

int morph_runtime_module_bootstrap(
    morph_runtime_module *module,
    morph_host *host,
    const morph_app_api *api,
    unsigned int render_mode,
    char *error,
    unsigned long error_capacity)
{
    if (!module || module->api) {
        morph_set_error(error, error_capacity,
            "Runtime module bootstrap requires an empty module");
        return 0;
    }
    return morph_runtime_module_stage_static_candidate(
            module, api, render_mode, error, error_capacity) &&
        morph_runtime_module_activate_candidate_with_state(
            module, host, NULL, 0, error, error_capacity);
}

int morph_runtime_module_has_candidate(const morph_runtime_module *module)
{
    return module->pending_api != NULL;
}

unsigned int morph_runtime_module_render_mode(const morph_runtime_module *module)
{
    return module->api ? module->render_mode : MORPHEUS_RENDER_EMBEDDED;
}

const char *morph_runtime_stage_name(morph_runtime_stage stage)
{
    switch (stage) {
    case MORPH_RUNTIME_STAGE_IDLE: return "idle";
    case MORPH_RUNTIME_STAGE_COMPILE: return "compile";
    case MORPH_RUNTIME_STAGE_VALIDATE: return "validate";
    case MORPH_RUNTIME_STAGE_SAVE_STATE: return "save-state";
    case MORPH_RUNTIME_STAGE_INITIALIZE: return "initialize";
    case MORPH_RUNTIME_STAGE_MIGRATE: return "migrate";
    case MORPH_RUNTIME_STAGE_ACTIVE: return "active";
    default: return "unknown";
    }
}

int morph_runtime_module_reload(
    morph_runtime_module *module,
    morph_host *host,
    const char *source_path,
    char *error,
    unsigned long error_capacity)
{
    return morph_runtime_module_compile_candidate(
            module,
            source_path,
            error,
            error_capacity) &&
        morph_runtime_module_activate_candidate(
            module,
            host,
            error,
            error_capacity);
}

void morph_runtime_module_update(
    morph_runtime_module *module,
    morph_host *host,
    double dt)
{
    if (module->api && module->api->update) {
        module->api->update(host, module->state, dt);
    }
}

void morph_runtime_module_render_ui(
    morph_runtime_module *module,
    morph_host *host)
{
    if (module->api && module->api->render_ui) {
        module->api->render_ui(host, module->state);
    }
}

void morph_runtime_module_destroy(
    morph_runtime_module *module,
    morph_host *host)
{
    morph_discard_pending(module);
    morph_release_candidate(
        (TCCState *)module->compiler,
        module->api,
        host,
        module->state);
#ifdef MORPHEUS_ENABLE_RUNTIME_LEAKCHECK
    (void)morph_runtime_leakcheck_report();
#endif
    morph_runtime_module_init(module);
}
