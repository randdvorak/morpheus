#include "morpheus/app_api.h"

#include <stdlib.h>
#include <string.h>

typedef struct json_state {
    char *message;
} json_state;

static int create(morph_host *host, void **state)
{
    static const char source[] = "{\"message\":\"TinyCC JSON available\"}";
    morph_json_document *document;
    const morph_json_value *root;
    const morph_json_value *message;
    const char *text;
    unsigned long size;
    json_state *app;
    (void)host;

    document = morph_json_parse(source, sizeof(source) - 1, 0);
    root = morph_json_root(document);
    message = morph_json_object_get(root, "message");
    text = morph_json_get_string(message, &size);
    if (!document || !text) {
        morph_json_document_free(document);
        return 0;
    }
    app = (json_state *)malloc(sizeof(*app));
    if (!app) {
        morph_json_document_free(document);
        return 0;
    }
    app->message = (char *)malloc(size + 1);
    if (!app->message) {
        free(app);
        morph_json_document_free(document);
        return 0;
    }
    memcpy(app->message, text, size);
    app->message[size] = '\0';
    morph_json_document_free(document);
    *state = app;
    return 1;
}

static void destroy(morph_host *host, void *state)
{
    json_state *app = (json_state *)state;
    (void)host;
    if (!app) return;
    free(app->message);
    free(app);
}

static void render(morph_host *host, void *state)
{
    json_state *app = (json_state *)state;
    host->ui_label(host, app->message);
}

static const morph_app_api app_api = {
    MORPHEUS_APP_ABI_VERSION,
    "json-smoke",
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
