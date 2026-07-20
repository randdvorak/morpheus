#ifndef MORPHEUS_RUNTIME_H
#define MORPHEUS_RUNTIME_H

#include "morpheus/app_api.h"

typedef struct morph_runtime_config {
    const char *fallback_name;
    const char *fallback_bundle_identifier;
    unsigned int render_mode;
    int window_width;
    int window_height;
    const morph_capability_registry *capabilities;
} morph_runtime_config;

/* Run one ahead-of-time compiled application in the standalone runtime. */
int morph_runtime_run(
    const morph_app_api *api,
    const morph_runtime_config *config);

#endif
