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
    morph_runtime_module *module,
    morph_host *host,
    const char *name,
    const char *label)
{
    observed_label[0] = '\0';
    morph_runtime_module_render_ui(module, host);
    return module->api &&
        strcmp(module->api->name, name) == 0 &&
        strcmp(observed_label, label) == 0;
}

int main(void)
{
    morph_runtime_module module;
    morph_host host = {
        MORPHEUS_HOST_ABI_VERSION,
        0,
        test_log,
        test_label,
        test_button,
        0,
        0
    };
    struct nk_context nuklear;
    struct nk_user_font font = {0};
    char error[4096];
    void *version_one_compiler;
    int restored_state = 17;

    font.height = 15.0f;
    font.width = test_font_width;
    font.query = test_font_glyph;
    if (!nk_init_default(&nuklear, &font)) {
        fprintf(stderr, "unable to initialize Nuklear smoke context\n");
        return 1;
    }
    host.nuklear = &nuklear;
    morph_runtime_module_init(&module);

    if (!morph_runtime_module_reload(
            &module,
            &host,
            MORPHEUS_TEST_FIXTURE_ROOT "/module_stdlib.c",
            error,
            sizeof(error)) ||
        !expect_active(&module, &host, "stdlib-smoke", "TinyCC stdlib available")) {
        fprintf(stderr, "stdlib module failed to load: %s\n", error);
        return 2;
    }
    morph_runtime_module_destroy(&module, &host);

    if (!morph_runtime_module_reload(
            &module,
            &host,
            MORPHEUS_TEST_FIXTURE_ROOT "/module_json.c",
            error,
            sizeof(error)) ||
        !expect_active(&module, &host, "json-smoke", "TinyCC JSON available")) {
        fprintf(stderr, "JSON facade module failed to load: %s\n", error);
        return 13;
    }
    morph_runtime_module_destroy(&module, &host);

    if (!morph_runtime_module_reload(
            &module,
            &host,
            MORPHEUS_TEST_FIXTURE_ROOT "/module_nuklear.c",
            error,
            sizeof(error)) ||
        morph_runtime_module_render_mode(&module) != MORPHEUS_RENDER_NUKLEAR_WINDOWS) {
        fprintf(stderr, "Nuklear module failed to load: %s\n", error);
        return 3;
    }
    morph_runtime_module_render_ui(&module, &host);
    morph_runtime_module_destroy(&module, &host);

    if (!morph_runtime_module_reload(
            &module,
            &host,
            MORPHEUS_TEST_FIXTURE_ROOT "/module_v1.c",
            error,
            sizeof(error)) ||
        !expect_active(&module, &host, "version-one", "version one: 41")) {
        fprintf(stderr, "initial load failed: %s\n", error);
        return 4;
    }
    if (module.last_stage != MORPH_RUNTIME_STAGE_ACTIVE) {
        fprintf(stderr, "initial load did not report the active stage\n");
        return 5;
    }
    version_one_compiler = module.compiler;

    if (morph_runtime_module_compile_candidate(
            &module,
            MORPHEUS_TEST_FIXTURE_ROOT "/module_invalid.c",
            error,
            sizeof(error)) ||
        morph_runtime_module_has_candidate(&module) ||
        module.last_stage != MORPH_RUNTIME_STAGE_VALIDATE ||
        !expect_active(&module, &host, "version-one", "version one: 41")) {
        fprintf(stderr, "invalid candidate replaced the active module\n");
        return 6;
    }

    if (morph_runtime_module_compile_candidate(
            &module,
            MORPHEUS_TEST_FIXTURE_ROOT "/module_compile_error.c",
            error,
            sizeof(error)) ||
        morph_runtime_module_has_candidate(&module) ||
        module.last_stage != MORPH_RUNTIME_STAGE_COMPILE ||
        !expect_active(&module, &host, "version-one", "version one: 41")) {
        fprintf(stderr, "compiler failure disturbed the active module\n");
        return 7;
    }

    if (!morph_runtime_module_compile_candidate(
            &module,
            MORPHEUS_TEST_FIXTURE_ROOT "/module_init_failure.c",
            error,
            sizeof(error)) ||
        !morph_runtime_module_has_candidate(&module) ||
        morph_runtime_module_activate_candidate(
            &module,
            &host,
            error,
            sizeof(error)) ||
        morph_runtime_module_has_candidate(&module) ||
        module.last_stage != MORPH_RUNTIME_STAGE_INITIALIZE ||
        !expect_active(&module, &host, "version-one", "version one: 41")) {
        fprintf(stderr, "initialization failure disturbed the active module\n");
        return 8;
    }

    if (!morph_runtime_module_compile_candidate(
            &module,
            MORPHEUS_TEST_FIXTURE_ROOT "/module_migration_failure.c",
            error,
            sizeof(error)) ||
        morph_runtime_module_activate_candidate(
            &module,
            &host,
            error,
            sizeof(error)) ||
        module.last_stage != MORPH_RUNTIME_STAGE_MIGRATE ||
        !expect_active(&module, &host, "version-one", "version one: 41")) {
        fprintf(stderr, "migration failure disturbed the active module\n");
        return 9;
    }

    if (!morph_runtime_module_compile_candidate(
            &module,
            MORPHEUS_TEST_FIXTURE_ROOT "/module_v2.c",
            error,
            sizeof(error)) ||
        !morph_runtime_module_has_candidate(&module) ||
        module.compiler != version_one_compiler ||
        !expect_active(&module, &host, "version-one", "version one: 41")) {
        fprintf(stderr, "candidate compilation was not isolated: %s\n", error);
        return 10;
    }

    if (!morph_runtime_module_activate_candidate(
            &module,
            &host,
            error,
            sizeof(error)) ||
        module.compiler == version_one_compiler ||
        morph_runtime_module_has_candidate(&module) ||
        !expect_active(
            &module,
            &host,
            "version-two",
            "version two: migrated 41")) {
        fprintf(stderr, "stateful activation failed: %s\n", error);
        return 11;
    }

    if (!morph_runtime_module_compile_candidate(
            &module,
            MORPHEUS_TEST_FIXTURE_ROOT "/module_v2.c",
            error,
            sizeof(error)) ||
        !morph_runtime_module_activate_candidate_with_state(
            &module,
            &host,
            &restored_state,
            sizeof(restored_state),
            error,
            sizeof(error)) ||
        module.last_stage != MORPH_RUNTIME_STAGE_ACTIVE ||
        !expect_active(
            &module,
            &host,
            "version-two",
            "version two: restored 17")) {
        fprintf(stderr, "explicit checkpoint restoration failed: %s\n", error);
        return 12;
    }

    morph_runtime_module_destroy(&module, &host);
    nk_free(&nuklear);
    puts("PASS: transactional activation, migration, and checkpoint restoration");
    return 0;
}
