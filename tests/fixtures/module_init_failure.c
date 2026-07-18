#include "morpheus/app_api.h"

static int create(morph_host *host, void **state)
{
    (void)host;
    *state = 0;
    return 0;
}

static void render(morph_host *host, void *state)
{
    (void)host;
    (void)state;
}

static const morph_app_api app_api = {
    MORPHEUS_APP_ABI_VERSION,
    "init-failure",
    create,
    0,
    0,
    render,
    0,
    0
};

const morph_app_api *morph_app_entry(void)
{
    return &app_api;
}
