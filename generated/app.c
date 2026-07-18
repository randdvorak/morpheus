#include "morpheus/app_api.h"

#define MAX_POINTS 64

static const char request_url[] =
    "https://api.fxratesapi.com/timeseries?start_date=2026-07-04T22:44:12.324Z&end_date=2026-07-18T22:44:12.324Z";

typedef struct app_state {
    morph_http_request_id request;
    int loading;
    int failed;
    int count;
    int currency;
    long status_code;
    float values[MAX_POINTS];
    char dates[MAX_POINTS][11];
    char message[96];
} app_state;

static app_state state_storage;
static const char *currency_codes[] = {"EUR", "GBP", "JPY", "CAD"};

static void copy_text(char *dst, const char *src, int capacity)
{
    int i = 0;
    if (!src) src = "Unknown error";
    while (i + 1 < capacity && src[i]) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

static const char *skip_space(const char *p, const char *end)
{
    while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) ++p;
    return p;
}

static int match_at(const char *p, const char *end, const char *word)
{
    while (*word) {
        if (p >= end || *p != *word) return 0;
        ++p;
        ++word;
    }
    return 1;
}

static const char *find_text(const char *p, const char *end, const char *word)
{
    while (p < end) {
        if (match_at(p, end, word)) return p;
        ++p;
    }
    return (const char *)0;
}

static int parse_number(const char *p, const char *end, float *value)
{
    double result = 0.0;
    double scale = 1.0;
    int sign = 1;
    int digits = 0;
    p = skip_space(p, end);
    if (p < end && *p == '-') { sign = -1; ++p; }
    while (p < end && *p >= '0' && *p <= '9') {
        result = result * 10.0 + (double)(*p - '0');
        ++p;
        digits = 1;
    }
    if (p < end && *p == '.') {
        ++p;
        while (p < end && *p >= '0' && *p <= '9') {
            scale *= 0.1;
            result += (double)(*p - '0') * scale;
            ++p;
            digits = 1;
        }
    }
    if (!digits) return 0;
    *value = (float)(result * (double)sign);
    return 1;
}

static int parse_rates(app_state *app, const char *body, unsigned long size)
{
    const char *end = body + size;
    const char *p = find_text(body, end, "\"rates\"");
    const char *code = currency_codes[app->currency];
    int count = 0;
    if (!p) return 0;
    p = find_text(p, end, "{");
    if (!p) return 0;
    ++p;
    while (p < end && count < MAX_POINTS) {
        const char *date;
        const char *object_end;
        const char *rate;
        int i;
        p = skip_space(p, end);
        if (p >= end || *p == '}') break;
        if (*p != '"') { ++p; continue; }
        date = ++p;
        if (end - date < 10 || date[4] != '-' || date[7] != '-') {
            p = find_text(p, end, "}");
            if (!p) break;
            ++p;
            continue;
        }
        object_end = find_text(p, end, "}");
        if (!object_end) break;
        rate = find_text(p, object_end, code);
        if (rate) {
            rate = find_text(rate, object_end, ":");
            if (rate && parse_number(rate + 1, object_end, &app->values[count])) {
                for (i = 0; i < 10; ++i) app->dates[count][i] = date[i];
                app->dates[count][10] = '\0';
                ++count;
            }
        }
        p = object_end + 1;
    }
    app->count = count;
    return count > 0;
}

static void begin_request(morph_host *host, app_state *app)
{
    if (!host->http) {
        app->failed = 1;
        copy_text(app->message, "HTTP service is unavailable", 96);
        return;
    }
    if (app->request) morph_http_cancel(host->http, app->request);
    app->request = morph_http_get(host->http, request_url);
    app->loading = app->request != 0;
    app->failed = app->request == 0;
    app->count = 0;
    app->status_code = 0;
    copy_text(app->message, app->loading ? "Fetching exchange rates..." : "Could not start request", 96);
}

static int app_create(morph_host *host, void **state)
{
    app_state *app = &state_storage;
    int i;
    unsigned char *bytes = (unsigned char *)app;
    for (i = 0; i < (int)sizeof(*app); ++i) bytes[i] = 0;
    *state = app;
    begin_request(host, app);
    host->log(host, "FX Rates chart loaded");
    return 1;
}

static void app_destroy(morph_host *host, void *state)
{
    app_state *app = (app_state *)state;
    if (host->http && app->request) morph_http_cancel(host->http, app->request);
}

static void app_update(morph_host *host, void *state, double dt)
{
    app_state *app = (app_state *)state;
    morph_http_result result;
    (void)dt;
    if (!app->loading || !host->http || !app->request) return;
    if (!morph_http_poll(host->http, app->request, &result) || !result.completed) return;
    app->loading = 0;
    app->status_code = result.status_code;
    if (result.failed || result.status_code < 200 || result.status_code >= 300) {
        app->failed = 1;
        copy_text(app->message, result.error ? result.error : "The rates service returned an error", 96);
    } else if (!parse_rates(app, result.body, result.body_size)) {
        app->failed = 1;
        copy_text(app->message, "No rates found for the selected currency", 96);
    } else {
        app->failed = 0;
        copy_text(app->message, "Rates updated", 96);
    }
}

static void app_render_ui(morph_host *host, void *state)
{
    app_state *app = (app_state *)state;
    struct nk_context *ctx = host->nuklear;
    int i;
    float low, high, pad;
    if (!ctx) {
        host->ui_label(host, app->message);
        if (host->ui_button(host, "Retry")) begin_request(host, app);
        return;
    }
    nk_layout_row_dynamic(ctx, 32.0f, 1);
    nk_label(ctx, "FX Rates - USD base", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, 28.0f, 4);
    for (i = 0; i < 4; ++i) {
        if (nk_option_label(ctx, currency_codes[i], app->currency == i)) {
            if (app->currency != i) {
                app->currency = i;
                begin_request(host, app);
            }
        }
    }
    nk_layout_row_dynamic(ctx, 24.0f, 1);
    nk_label(ctx, app->message, app->failed ? NK_TEXT_LEFT : NK_TEXT_LEFT);
    if (app->loading) {
        nk_layout_row_dynamic(ctx, 45.0f, 1);
        nk_label(ctx, "Loading...", NK_TEXT_CENTERED);
    } else if (app->count > 0) {
        low = high = app->values[0];
        for (i = 1; i < app->count; ++i) {
            if (app->values[i] < low) low = app->values[i];
            if (app->values[i] > high) high = app->values[i];
        }
        pad = (high - low) * 0.08f;
        if (pad <= 0.0f) pad = high * 0.01f + 0.001f;
        nk_layout_row_dynamic(ctx, 260.0f, 1);
        if (nk_chart_begin(ctx, NK_CHART_LINES, app->count, low - pad, high + pad)) {
            for (i = 0; i < app->count; ++i) (void)nk_chart_push(ctx, app->values[i]);
            nk_chart_end(ctx);
        }
        nk_layout_row_dynamic(ctx, 24.0f, 2);
        nk_labelf(ctx, NK_TEXT_LEFT, "%s  %.4f", app->dates[0], app->values[0]);
        nk_labelf(ctx, NK_TEXT_RIGHT, "%s  %.4f", app->dates[app->count - 1], app->values[app->count - 1]);
        nk_layout_row_dynamic(ctx, 24.0f, 1);
        nk_labelf(ctx, NK_TEXT_LEFT, "Low %.4f    High %.4f    %d daily points", low, high, app->count);
    }
    nk_layout_row_dynamic(ctx, 30.0f, 1);
    if (nk_button_label(ctx, "Refresh rates")) begin_request(host, app);
}

static int app_save_state(morph_host *host, void *state, const void **data, unsigned long *size)
{
    (void)host;
    *data = state;
    *size = sizeof(app_state);
    return 1;
}

static int app_load_state(morph_host *host, void **state, const void *data, unsigned long size)
{
    app_state *app = (app_state *)*state;
    const app_state *saved = (const app_state *)data;
    if (size == sizeof(app_state) && saved->currency >= 0 && saved->currency < 4)
        app->currency = saved->currency;
    begin_request(host, app);
    return 1;
}

static const morph_app_api app_api = {
    MORPHEUS_APP_ABI_VERSION,
    "FX Rates Graph",
    app_create,
    app_destroy,
    app_update,
    app_render_ui,
    app_save_state,
    app_load_state
};

const morph_app_api *morph_app_entry(void) { return &app_api; }
