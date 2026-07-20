#include <stdio.h>
#include <string.h>

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_INCLUDE_COMMAND_USERDATA
#define NK_IMPLEMENTATION
#include "nuklear.h"

#include "morpheus/app_api.h"
#include "authoring_capabilities.h"
#include "runtime_module.h"

static char observed_label[64];

static void test_log(morph_host *host, const char *message)
{
    (void)host;
    (void)message;
}

static void test_label(morph_host *host, const char *text)
{
    (void)host;
    snprintf(observed_label, sizeof(observed_label), "%s", text);
}

static int test_button(morph_host *host, const char *text)
{
    (void)host;
    (void)text;
    return 0;
}

static float test_font_width(nk_handle handle, float height, const char *text, int length)
{
    (void)handle;
    (void)height;
    (void)text;
    return (float)length * 8.0f;
}

static void test_font_glyph(
    nk_handle handle,
    float height,
    struct nk_user_font_glyph *glyph,
    nk_rune codepoint,
    nk_rune next_codepoint)
{
    (void)handle;
    (void)codepoint;
    (void)next_codepoint;
    glyph->uv[0] = nk_vec2(0, 0);
    glyph->uv[1] = nk_vec2(1, 1);
    glyph->offset = nk_vec2(0, 0);
    glyph->width = height;
    glyph->height = height;
    glyph->xadvance = height;
}

static int expect_active(
    const morph_authoring_modules_api *modules,
    void *context,
    morph_host *host,
    const char *name,
    const char *label)
{
    observed_label[0] = '\0';
    modules->render_ui(context, host);
    return modules->is_active(context) && modules->active_name(context) &&
        strcmp(modules->active_name(context), name) == 0 &&
        strcmp(observed_label, label) == 0;
}

int main(void)
{
    morph_runtime_module module;
    morph_capability module_capability;
    morph_capability_registry registry;
    morph_host authoring_host = {0};
    const morph_capability *provider;
    const morph_authoring_modules_api *modules;
    void *module_context;
    morph_host host = {
        .abi_version = MORPHEUS_HOST_ABI_VERSION,
        .log = test_log,
        .ui_label = test_label,
        .ui_button = test_button
    };
    struct nk_context nuklear;
    struct nk_user_font font = {0};
    char error[4096];
    int restored_state = 17;

    font.height = 15.0f;
    font.width = test_font_width;
    font.query = test_font_glyph;
    if (!nk_init_default(&nuklear, &font)) {
        fprintf(stderr, "unable to initialize Nuklear smoke context\n");
        return 1;
    }
    host.nuklear = &nuklear;
    module_capability = morph_authoring_modules_capability(&module);
    registry.entries = &module_capability;
    registry.count = 1;
    authoring_host.abi_version = MORPHEUS_HOST_ABI_VERSION;
    authoring_host.capabilities = &registry;
    provider = morph_host_find_capability(
        &authoring_host,
        MORPHEUS_AUTHORING_MODULES_CAPABILITY,
        MORPHEUS_AUTHORING_MODULES_ABI_VERSION);
    modules = morph_authoring_modules_from_capability(provider);
    module_context = provider ? provider->context : NULL;
    if (!modules || host.capabilities) {
        fprintf(stderr, "module capability discovery failed\n");
        return 14;
    }
    modules->init(module_context);

    if (!modules->reload(
            module_context,
            &host,
            MORPHEUS_TEST_FIXTURE_ROOT "/module_stdlib.c",
            error,
            sizeof(error)) ||
        !expect_active(
            modules, module_context, &host,
            "stdlib-smoke", "TinyCC stdlib available")) {
        fprintf(stderr, "stdlib module failed to load: %s\n", error);
        return 2;
    }
    modules->destroy(module_context, &host);

    if (!modules->reload(
            module_context,
            &host,
            MORPHEUS_TEST_FIXTURE_ROOT "/module_json.c",
            error,
            sizeof(error)) ||
        !expect_active(
            modules, module_context, &host,
            "json-smoke", "TinyCC JSON available")) {
        fprintf(stderr, "JSON facade module failed to load: %s\n", error);
        return 13;
    }
    modules->destroy(module_context, &host);

    if (!modules->reload(
            module_context,
            &host,
            MORPHEUS_TEST_FIXTURE_ROOT "/module_nuklear.c",
            error,
            sizeof(error)) ||
        modules->render_mode(module_context) != MORPHEUS_RENDER_NUKLEAR_WINDOWS) {
        fprintf(stderr, "Nuklear module failed to load: %s\n", error);
        return 3;
    }
    modules->render_ui(module_context, &host);
    modules->destroy(module_context, &host);

    if (!modules->reload(
            module_context,
            &host,
            MORPHEUS_TEST_FIXTURE_ROOT "/module_v1.c",
            error,
            sizeof(error)) ||
        !expect_active(
            modules, module_context, &host,
            "version-one", "version one: 41")) {
        fprintf(stderr, "initial load failed: %s\n", error);
        return 4;
    }
    if (modules->last_stage(module_context) != MORPHEUS_AUTHORING_MODULE_ACTIVE) {
        fprintf(stderr, "initial load did not report the active stage\n");
        return 5;
    }
    if (modules->compile_candidate(
            module_context,
            MORPHEUS_TEST_FIXTURE_ROOT "/module_invalid.c",
            error,
            sizeof(error)) ||
        modules->has_candidate(module_context) ||
        modules->last_stage(module_context) != MORPHEUS_AUTHORING_MODULE_VALIDATE ||
        !expect_active(
            modules, module_context, &host,
            "version-one", "version one: 41")) {
        fprintf(stderr, "invalid candidate replaced the active module\n");
        return 6;
    }

    if (modules->compile_candidate(
            module_context,
            MORPHEUS_TEST_FIXTURE_ROOT "/module_compile_error.c",
            error,
            sizeof(error)) ||
        modules->has_candidate(module_context) ||
        modules->last_stage(module_context) != MORPHEUS_AUTHORING_MODULE_COMPILE ||
        !expect_active(
            modules, module_context, &host,
            "version-one", "version one: 41")) {
        fprintf(stderr, "compiler failure disturbed the active module\n");
        return 7;
    }

    if (!modules->compile_candidate(
            module_context,
            MORPHEUS_TEST_FIXTURE_ROOT "/module_init_failure.c",
            error,
            sizeof(error)) ||
        !modules->has_candidate(module_context) ||
        modules->activate_candidate(
            module_context,
            &host,
            error,
            sizeof(error)) ||
        modules->has_candidate(module_context) ||
        modules->last_stage(module_context) != MORPHEUS_AUTHORING_MODULE_INITIALIZE ||
        !expect_active(
            modules, module_context, &host,
            "version-one", "version one: 41")) {
        fprintf(stderr, "initialization failure disturbed the active module\n");
        return 8;
    }

    if (!modules->compile_candidate(
            module_context,
            MORPHEUS_TEST_FIXTURE_ROOT "/module_migration_failure.c",
            error,
            sizeof(error)) ||
        modules->activate_candidate(
            module_context,
            &host,
            error,
            sizeof(error)) ||
        modules->last_stage(module_context) != MORPHEUS_AUTHORING_MODULE_MIGRATE ||
        !expect_active(
            modules, module_context, &host,
            "version-one", "version one: 41")) {
        fprintf(stderr, "migration failure disturbed the active module\n");
        return 9;
    }

    if (!modules->compile_candidate(
            module_context,
            MORPHEUS_TEST_FIXTURE_ROOT "/module_v2.c",
            error,
            sizeof(error)) ||
        !modules->has_candidate(module_context) ||
        !expect_active(
            modules, module_context, &host,
            "version-one", "version one: 41")) {
        fprintf(stderr, "candidate compilation was not isolated: %s\n", error);
        return 10;
    }

    if (!modules->activate_candidate(
            module_context,
            &host,
            error,
            sizeof(error)) ||
        modules->has_candidate(module_context) ||
        !expect_active(
            modules,
            module_context,
            &host,
            "version-two",
            "version two: migrated 41")) {
        fprintf(stderr, "stateful activation failed: %s\n", error);
        return 11;
    }

    if (!modules->compile_candidate(
            module_context,
            MORPHEUS_TEST_FIXTURE_ROOT "/module_v2.c",
            error,
            sizeof(error)) ||
        !modules->activate_candidate_with_state(
            module_context,
            &host,
            &restored_state,
            sizeof(restored_state),
            error,
            sizeof(error)) ||
        modules->last_stage(module_context) != MORPHEUS_AUTHORING_MODULE_ACTIVE ||
        !expect_active(
            modules,
            module_context,
            &host,
            "version-two",
            "version two: restored 17")) {
        fprintf(stderr, "explicit checkpoint restoration failed: %s\n", error);
        return 12;
    }

    modules->destroy(module_context, &host);
    nk_free(&nuklear);
    puts("PASS: transactional activation, migration, and checkpoint restoration");
    return 0;
}
