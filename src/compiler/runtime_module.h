#ifndef MORPHEUS_RUNTIME_MODULE_H
#define MORPHEUS_RUNTIME_MODULE_H

#include "morpheus/app_api.h"

typedef enum morph_runtime_stage {
    MORPH_RUNTIME_STAGE_IDLE = 0,
    MORPH_RUNTIME_STAGE_COMPILE,
    MORPH_RUNTIME_STAGE_VALIDATE,
    MORPH_RUNTIME_STAGE_SAVE_STATE,
    MORPH_RUNTIME_STAGE_INITIALIZE,
    MORPH_RUNTIME_STAGE_MIGRATE,
    MORPH_RUNTIME_STAGE_ACTIVE
} morph_runtime_stage;

typedef struct morph_runtime_module {
    void *compiler;
    const morph_app_api *api;
    void *state;
    void *pending_compiler;
    const morph_app_api *pending_api;
    unsigned int render_mode;
    unsigned int pending_render_mode;
    morph_runtime_stage last_stage;
} morph_runtime_module;

void morph_runtime_module_init(morph_runtime_module *module);
int morph_runtime_module_stage_static_candidate(
    morph_runtime_module *module,
    const morph_app_api *api,
    unsigned int render_mode,
    char *error,
    unsigned long error_capacity);
int morph_runtime_module_bootstrap(
    morph_runtime_module *module,
    morph_host *host,
    const morph_app_api *api,
    unsigned int render_mode,
    char *error,
    unsigned long error_capacity);
int morph_runtime_module_compile_candidate(
    morph_runtime_module *module,
    const char *source_path,
    char *error,
    unsigned long error_capacity);
int morph_runtime_module_activate_candidate(
    morph_runtime_module *module,
    morph_host *host,
    char *error,
    unsigned long error_capacity);
int morph_runtime_module_activate_candidate_with_state(
    morph_runtime_module *module,
    morph_host *host,
    const void *state_data,
    unsigned long state_size,
    char *error,
    unsigned long error_capacity);
int morph_runtime_module_capture_state(
    morph_runtime_module *module,
    morph_host *host,
    const void **state_data,
    unsigned long *state_size,
    char *error,
    unsigned long error_capacity);
int morph_runtime_module_has_candidate(const morph_runtime_module *module);
unsigned int morph_runtime_module_render_mode(const morph_runtime_module *module);
const char *morph_runtime_stage_name(morph_runtime_stage stage);
int morph_runtime_module_reload(
    morph_runtime_module *module,
    morph_host *host,
    const char *source_path,
    char *error,
    unsigned long error_capacity);
void morph_runtime_module_update(
    morph_runtime_module *module,
    morph_host *host,
    double dt);
void morph_runtime_module_render_ui(
    morph_runtime_module *module,
    morph_host *host);
void morph_runtime_module_destroy(
    morph_runtime_module *module,
    morph_host *host);

#endif
