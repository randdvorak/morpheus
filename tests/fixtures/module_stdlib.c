#include "morpheus/app_api.h"
#include <stdlib.h>
#include <string.h>

typedef struct stdlib_state {
    char *message;
} stdlib_state;

static int create(morph_host *host, void **state)
{
    stdlib_state *app = (stdlib_state *)malloc(sizeof(*app));
    (void)host;
    if (!app) return 0;
    app->message = (char *)malloc(32);
    if (!app->message) {
        free(app);
        return 0;
    }
    strncpy(app->message, "TinyCC stdlib available", 31);
    app->message[31] = '\0';
    *state = app;
    return 1;
}

static void destroy(morph_host *host, void *state)
{
    stdlib_state *app = (stdlib_state *)state;
    (void)host;
    if (!app) return;
    free(app->message);
    free(app);
}

static void render(morph_host *host, void *state)
{
    stdlib_state *app = (stdlib_state *)state;
    host->ui_label(host, app->message);
}

static const morph_app_api app_api = {
    MORPHEUS_APP_ABI_VERSION,
    "stdlib-smoke",
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
