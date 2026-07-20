#include <stdlib.h>

#include "morpheus/runtime.h"

static int app_create(morph_host *host, void **state)
{
    (void)host;
    (void)state;
    return 1;
}

static void app_destroy(morph_host *host, void *state)
{
    (void)host;
    (void)state;
}

static void app_update(morph_host *host, void *state, double dt)
{
    (void)host;
    (void)state;
    (void)dt;
}

static void app_render(morph_host *host, void *state)
{
    (void)host;
    (void)state;
}

int main(void)
{
    const morph_app_api incomplete = {
        .abi_version = MORPHEUS_APP_ABI_VERSION,
        .name = "incomplete"
    };
    const morph_app_api complete = {
        MORPHEUS_APP_ABI_VERSION,
        "validation",
        app_create,
        app_destroy,
        app_update,
        app_render,
        NULL,
        NULL
    };
    const morph_runtime_config invalid_mode = {
        .fallback_name = "validation",
        .fallback_bundle_identifier = "dev.morpheus.validation",
        .render_mode = MORPHEUS_RENDER_NUKLEAR_WINDOWS + 1u,
        .window_width = 320,
        .window_height = 240,
        .capabilities = NULL
    };

    if (morph_runtime_run(NULL, NULL) != EXIT_FAILURE) return 1;
    if (morph_runtime_run(&incomplete, NULL) != EXIT_FAILURE) return 2;
    if (morph_runtime_run(&complete, &invalid_mode) != EXIT_FAILURE) return 3;
    return 0;
}
