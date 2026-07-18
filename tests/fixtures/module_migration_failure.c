#include "morpheus/app_api.h"

static int state_storage;

static int create(morph_host *host, void **state)
{
    (void)host;
    *state = &state_storage;
    return 1;
}

static void render(morph_host *host, void *state)
{
    (void)host;
    (void)state;
}

static int load_state(
    morph_host *host,
    void **state,
    const void *data,
    unsigned long size)
{
    (void)host;
    (void)state;
    (void)data;
    (void)size;
    return 0;
}

static const morph_app_api app_api = {
    MORPHEUS_APP_ABI_VERSION,
    "migration-failure",
    create,
    0,
    0,
    render,
    0,
    load_state
};

const morph_app_api *morph_app_entry(void)
{
    return &app_api;
}
