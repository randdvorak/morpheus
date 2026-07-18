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

int morph_runtime_module_reload(
    morph_runtime_module *module,
    morph_host *host,
    const char *source_path,
    char *error,
    unsigned long error_capacity)
{
    TCCState *candidate;
    morph_app_entry_fn entry;
    const morph_app_api *candidate_api;
    void *candidate_state = NULL;
    morph_error_sink sink = {error, error_capacity, 0};

    if (error && error_capacity) {
        error[0] = '\0';
    }

    candidate = tcc_new();
    if (!candidate) {
        snprintf(error, (size_t)error_capacity, "Unable to create TinyCC state");
        return 0;
    }

    tcc_set_error_func(candidate, &sink, morph_tcc_error);
    tcc_set_lib_path(candidate, MORPHEUS_TCC_LIB_PATH);
    tcc_set_options(candidate, "-nostdlib -Wall -Werror");
    tcc_add_include_path(candidate, MORPHEUS_SOURCE_ROOT "/include");

    if (tcc_set_output_type(candidate, TCC_OUTPUT_MEMORY) < 0 ||
        tcc_add_file(candidate, source_path) < 0 ||
        tcc_relocate(candidate) < 0) {
        morph_release_candidate(candidate, NULL, host, NULL);
        return 0;
    }

    entry = (morph_app_entry_fn)tcc_get_symbol(candidate, "morph_app_entry");
    if (!entry) {
        snprintf(error, (size_t)error_capacity,
            "Candidate does not export morph_app_entry");
        morph_release_candidate(candidate, NULL, host, NULL);
        return 0;
    }

    candidate_api = entry();
    if (!candidate_api ||
        candidate_api->abi_version != MORPHEUS_APP_ABI_VERSION ||
        !candidate_api->render_ui) {
        snprintf(error, (size_t)error_capacity,
            "Candidate returned an invalid application ABI");
        morph_release_candidate(candidate, NULL, host, NULL);
        return 0;
    }

    if (candidate_api->create &&
        !candidate_api->create(host, &candidate_state)) {
        snprintf(error, (size_t)error_capacity,
            "Candidate initialization failed");
        morph_release_candidate(candidate, candidate_api, host, candidate_state);
        return 0;
    }

    morph_runtime_module_destroy(module, host);
    module->compiler = candidate;
    module->api = candidate_api;
    module->state = candidate_state;
    return 1;
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
    morph_release_candidate(
        (TCCState *)module->compiler,
        module->api,
        host,
        module->state);
    morph_runtime_module_init(module);
}
