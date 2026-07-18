#include "runtime_module.h"

#include <stdio.h>
#include <string.h>

#include "libtcc.h"

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
}

int morph_runtime_module_compile_candidate(
    morph_runtime_module *module,
    const char *source_path,
    char *error,
    unsigned long error_capacity)
{
    TCCState *candidate;
    morph_app_entry_fn entry;
    const morph_app_api *candidate_api;
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

    if (tcc_set_output_type(candidate, TCC_OUTPUT_MEMORY) < 0 ||
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
    if (!candidate_api ||
        candidate_api->abi_version != MORPHEUS_APP_ABI_VERSION ||
        !candidate_api->render_ui) {
        morph_set_error(
            error,
            error_capacity,
            "Candidate returned an invalid application ABI");
        tcc_delete(candidate);
        return 0;
    }

    module->pending_compiler = candidate;
    module->pending_api = candidate_api;
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
    if (!module->pending_compiler || !module->pending_api) {
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
    module->pending_compiler = NULL;
    module->pending_api = NULL;

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

int morph_runtime_module_has_candidate(const morph_runtime_module *module)
{
    return module->pending_compiler != NULL;
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
    morph_runtime_module_init(module);
}
