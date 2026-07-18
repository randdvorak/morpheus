#include "morpheus/app_api.h"

static int state_storage;

static int create(morph_host *host, void **state)
{
    (void)host;
    state_storage = 0;
    *state = &state_storage;
    return 1;
}

static void render(morph_host *host, void *state)
{
    int value = *(int *)state;
    host->ui_label(host, value == 41 ? "version two: migrated 41" : "version two: bad");
}

static int load_state(
    morph_host *host,
    void **state,
    const void *data,
    unsigned long size)
{
    (void)host;
    if (size != sizeof(int)) {
        return 0;
    }
    **(int **)state = *(const int *)data;
    return 1;
}

static const morph_app_api app_api = {
    MORPHEUS_APP_ABI_VERSION,
    "version-two",
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
