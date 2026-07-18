#include "morpheus/app_api.h"

static int create(morph_host *host, void **state)
{
    (void)host;
    *state = 0;
    return 1;
}

static void render(morph_host *host, void *state)
{
    struct nk_context *ctx = host->nuklear;
    struct nk_color accent = nk_rgb(40, 120, 220);
    (void)state;
    if (!ctx) return;
    if (nk_begin(ctx, "Nuklear Smoke Test", nk_rect(30, 30, 320, 180),
            NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {
        nk_layout_row_dynamic(ctx, 28, 1);
        nk_label_colored(ctx, "Full Nuklear API linked", NK_TEXT_LEFT, accent);
        if (nk_button_label(ctx, "Smoke button")) {
            host->log(host, "Nuklear button pressed");
        }
    }
    nk_end(ctx);
}

static const morph_app_api app_api = {
    MORPHEUS_APP_ABI_VERSION,
    "nuklear-smoke",
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

unsigned int morph_app_render_mode(void)
{
    return MORPHEUS_RENDER_NUKLEAR_WINDOWS;
}
