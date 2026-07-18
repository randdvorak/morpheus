#ifndef MORPHEUS_RUNTIME_MODULE_H
#define MORPHEUS_RUNTIME_MODULE_H

#include "morpheus/app_api.h"

typedef struct morph_runtime_module {
    void *compiler;
    const morph_app_api *api;
    void *state;
    void *pending_compiler;
    const morph_app_api *pending_api;
} morph_runtime_module;

void morph_runtime_module_init(morph_runtime_module *module);
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
int morph_runtime_module_has_candidate(const morph_runtime_module *module);
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
