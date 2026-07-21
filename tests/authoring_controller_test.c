#include <stdio.h>
#include <string.h>

#include "authoring_controller.h"

typedef struct fake_revisions {
    unsigned long active;
    int checkpointed;
    int refreshed;
    int released;
    int recorded;
} fake_revisions;

typedef struct fake_modules {
    int compiled;
    int activated;
    int activated_with_state;
    unsigned char state;
} fake_modules;

static int revision_init(void *c, const char *p, char *e, unsigned long n)
{ (void)c; (void)p; (void)e; (void)n; return 1; }
static unsigned long revision_active(void *c)
{ return ((fake_revisions *)c)->active; }
static unsigned long revision_latest(void *c)
{ return ((fake_revisions *)c)->active; }
static int revision_checkpoint(void *c, const char *p, const void *d,
    unsigned long s, unsigned long *r, char *e, unsigned long n)
{
    fake_revisions *revision = c;
    (void)p; (void)e; (void)n;
    if (!d || s != 1) return 0;
    revision->checkpointed++;
    revision->active++;
    if (r) *r = revision->active;
    return 1;
}
static int revision_load(void *c, unsigned long r, char *p, unsigned long pn,
    void **d, unsigned long *s, char *e, unsigned long n)
{
    static unsigned char state = 42;
    (void)c; (void)e; (void)n;
    if (!p || pn < 12 || !d || !s || r != 2) return 0;
    snprintf(p, (size_t)pn, "revision.c");
    *d = &state;
    *s = sizeof(state);
    return 1;
}
static void revision_release(void *c, void *d)
{ (void)d; ((fake_revisions *)c)->released++; }
static int revision_previous(void *c, unsigned long *r)
{ (void)c; *r = 2; return 1; }
static int revision_set_active(void *c, unsigned long r, char *e, unsigned long n)
{ (void)e; (void)n; ((fake_revisions *)c)->active = r; return 1; }
static int revision_begin(void *c, int *r, char *e, unsigned long n)
{ (void)c; (void)e; (void)n; *r = 0; return 1; }
static int revision_simple(void *c, char *e, unsigned long n)
{ (void)c; (void)e; (void)n; return 1; }
static int revision_refresh(void *c, char *e, unsigned long n)
{
    (void)e; (void)n;
    ((fake_revisions *)c)->refreshed++;
    return 1;
}
static int revision_record(void *c, const char *s, int ok, const char *m,
    char *e, unsigned long n)
{
    (void)s; (void)ok; (void)m; (void)e; (void)n;
    ((fake_revisions *)c)->recorded++;
    return 1;
}

static void module_init(void *c) { (void)c; }
static int module_compile(void *c, const char *p, char *e, unsigned long n)
{
    (void)e; (void)n;
    if (!p || !*p) return 0;
    ((fake_modules *)c)->compiled++;
    return 1;
}
static int module_activate(void *c, morph_host *h, char *e, unsigned long n)
{
    (void)e; (void)n;
    if (!h) return 0;
    ((fake_modules *)c)->activated++;
    return 1;
}
static int module_activate_state(void *c, morph_host *h, const void *d,
    unsigned long s, char *e, unsigned long n)
{
    (void)e; (void)n;
    if (!h || !d || s != 1) return 0;
    ((fake_modules *)c)->activated_with_state++;
    return 1;
}
static int module_capture(void *c, morph_host *h, const void **d,
    unsigned long *s, char *e, unsigned long n)
{
    fake_modules *module = c;
    (void)e; (void)n;
    if (!h || !d || !s) return 0;
    *d = &module->state;
    *s = sizeof(module->state);
    return 1;
}
static int module_true(void *c) { (void)c; return 1; }
static const char *module_name(void *c) { (void)c; return "Fake App"; }
static unsigned int module_render_mode(void *c)
{ (void)c; return MORPHEUS_RENDER_EMBEDDED; }
static morph_authoring_module_stage module_stage(void *c)
{ (void)c; return MORPHEUS_AUTHORING_MODULE_ACTIVE; }
static const char *module_stage_name(void *c, morph_authoring_module_stage s)
{ (void)c; (void)s; return "active"; }
static int module_reload(void *c, morph_host *h, const char *p, char *e,
    unsigned long n)
{ return module_compile(c, p, e, n) && module_activate(c, h, e, n); }
static void module_update(void *c, morph_host *h, double dt)
{ (void)c; (void)h; (void)dt; }
static void module_render(void *c, morph_host *h) { (void)c; (void)h; }
static void module_destroy(void *c, morph_host *h) { (void)c; (void)h; }

static const morph_authoring_revisions_api revisions_api = {
    MORPHEUS_AUTHORING_REVISIONS_ABI_VERSION,
    sizeof(morph_authoring_revisions_api),
    revision_init,
    revision_active,
    revision_latest,
    revision_checkpoint,
    revision_load,
    revision_release,
    revision_previous,
    revision_set_active,
    revision_begin,
    revision_refresh,
    revision_simple,
    revision_record
};

static const morph_authoring_modules_api modules_api = {
    MORPHEUS_AUTHORING_MODULES_ABI_VERSION,
    sizeof(morph_authoring_modules_api),
    module_init,
    module_compile,
    module_activate,
    module_activate_state,
    module_capture,
    module_true,
    module_true,
    module_name,
    module_render_mode,
    module_stage,
    module_stage_name,
    module_reload,
    module_update,
    module_render,
    module_destroy
};

int main(void)
{
    fake_revisions revisions = {.active = 2};
    fake_modules modules = {.state = 7};
    morph_capability entries[2] = {
        {
            MORPHEUS_AUTHORING_REVISIONS_CAPABILITY,
            MORPHEUS_AUTHORING_REVISIONS_ABI_VERSION,
            sizeof(revisions_api),
            &revisions_api,
            &revisions
        },
        {
            MORPHEUS_AUTHORING_MODULES_CAPABILITY,
            MORPHEUS_AUTHORING_MODULES_ABI_VERSION,
            sizeof(modules_api),
            &modules_api,
            &modules
        }
    };
    morph_capability_registry registry = {entries, 2};
    morph_host runtime_host = {.abi_version = MORPHEUS_HOST_ABI_VERSION};
    morph_authoring_controller controller;
    morph_capability capability;
    const morph_authoring_controller_api *api;
    morph_authoring_snapshot snapshot = {.struct_size = sizeof(snapshot)};
    morph_authoring_request request = {.struct_size = sizeof(request)};
    char error[128];

    if (!morph_authoring_controller_init(
            &controller, &registry, &runtime_host)) return 1;
    snprintf(controller.source_path, sizeof(controller.source_path), "current.c");
    capability = morph_authoring_controller_capability(&controller);
    api = morph_authoring_controller_from_capability(&capability);
    morph_authoring_controller_set_availability(&controller, 1, 1);
    if (!api || !api->snapshot(&controller, &snapshot) ||
        !snapshot.can_recompile || !snapshot.can_rollback ||
        strcmp(snapshot.active_name, "Fake App") != 0) return 2;

    request.command = MORPHEUS_AUTHORING_COMMAND_RECOMPILE;
    if (!api->dispatch(
            &controller, &request,
            error, sizeof(error)) ||
        !morph_authoring_controller_has_pending(&controller) ||
        !morph_authoring_controller_tick(&controller) ||
        modules.compiled != 1 || modules.activated != 1 ||
        revisions.checkpointed != 1 || revisions.active != 3 ||
        revisions.refreshed != 1 || revisions.recorded != 1) return 3;

    request.command = MORPHEUS_AUTHORING_COMMAND_ROLLBACK;
    if (!api->dispatch(
            &controller, &request,
            error, sizeof(error)) ||
        !morph_authoring_controller_tick(&controller) ||
        modules.compiled != 2 || modules.activated_with_state != 1 ||
        revisions.active != 2 || revisions.refreshed != 2 ||
        revisions.released != 1 || revisions.recorded != 2) return 4;

    snapshot.struct_size = sizeof(snapshot);
    if (!api->snapshot(&controller, &snapshot) || snapshot.command_pending ||
        strcmp(snapshot.message, "Rollback succeeded") != 0) return 5;
    puts("PASS: authoring controller composes recompile and rollback transactions");
    return 0;
}
