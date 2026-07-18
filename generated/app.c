#include "morpheus/app_api.h"

typedef struct app_state {
    unsigned int button_presses;
} app_state;

static app_state state_storage;

static int app_create(morph_host *host, void **state)
{
    state_storage.button_presses = 0;
    *state = &state_storage;
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
    app_state *app = (app_state *)state;
    host->ui_label(host, "This interface was compiled at runtime by TinyCC.");
    if (app->button_presses) {
        host->ui_label(host, "Generated state survived the last hot reload.");
    }
    if (host->ui_button(host, "Generated button")) {
        app->button_presses += 1;
        host->log(host, "Generated button pressed");
    }
}

static int app_save_state(
    morph_host *host,
    void *state,
    const void **data,
    unsigned long *size)
{
    (void)host;
    *data = state;
    *size = sizeof(app_state);
    return 1;
}

static int app_load_state(
    morph_host *host,
    void **state,
    const void *data,
    unsigned long size)
{
    const app_state *saved = (const app_state *)data;
    app_state *app = (app_state *)*state;
    (void)host;

    if (size != sizeof(app_state)) {
        return 0;
    }
    app->button_presses = saved->button_presses;
    return 1;
}

static const morph_app_api app_api = {
    MORPHEUS_APP_ABI_VERSION,
    "Seed Application",
    app_create,
    app_destroy,
    app_update,
    app_render_ui,
    app_save_state,
    app_load_state
};

const morph_app_api *morph_app_entry(void)
{
    return &app_api;
}
