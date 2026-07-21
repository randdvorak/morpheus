#include "morpheus/app_api.h"
#include <string.h>

static char message[32];

static int create(morph_host *host, void **state)
{
    (void)host;
    strcpy(message, "unbounded copy unavailable");
    *state = message;
    return 1;
}

static void destroy(morph_host *host, void *state)
{
    (void)host;
    (void)state;
}

static void render(morph_host *host, void *state)
{
    host->ui_label(host, (const char *)state);
}

static const morph_app_api app_api = {
    MORPHEUS_APP_ABI_VERSION,
    "forbidden-stdlib",
    create,
    destroy,
    0,
    render,
    0,
    0
};

const morph_app_api *morph_app_entry(void)
{
    return &app_api;
}
