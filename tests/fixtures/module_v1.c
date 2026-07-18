#include "morpheus/app_api.h"

static void render(morph_host *host, void *state)
{
    (void)state;
    host->ui_label(host, "version one");
}

static const morph_app_api app_api = {
    MORPHEUS_APP_ABI_VERSION,
    "version-one",
    0,
    0,
    0,
    render
};

const morph_app_api *morph_app_entry(void)
{
    return &app_api;
}
