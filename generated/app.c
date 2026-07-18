#include "morpheus/app_api.h"

static int app_create(morph_host *host, void **state)
{
    *state = 0;
    host->log(host, "Generated module loaded");
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

static void app_render_ui(morph_host *host, void *state)
{
    (void)state;
    host->ui_label(host, "This interface was compiled at runtime by TinyCC.");
    if (host->ui_button(host, "Generated button")) {
        host->log(host, "Generated button pressed");
    }
}

static const morph_app_api app_api = {
    MORPHEUS_APP_ABI_VERSION,
    "Seed Application",
    app_create,
    app_destroy,
    app_update,
    app_render_ui
};

const morph_app_api *morph_app_entry(void)
{
    return &app_api;
}
