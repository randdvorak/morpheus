#include <stdio.h>
#include <string.h>

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
        test_button
    };
    char error[4096];
    void *version_one_compiler;
    int restored_state = 17;

    morph_runtime_module_init(&module);

    if (!morph_runtime_module_reload(
            &module,
            &host,
            MORPHEUS_TEST_FIXTURE_ROOT "/module_v1.c",
            error,
            sizeof(error)) ||
        !expect_active(&module, &host, "version-one", "version one: 41")) {
        fprintf(stderr, "initial load failed: %s\n", error);
        return 1;
    }
    if (module.last_stage != MORPH_RUNTIME_STAGE_ACTIVE) {
        fprintf(stderr, "initial load did not report the active stage\n");
        return 2;
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
        return 3;
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
        return 4;
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
        return 5;
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
        return 6;
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
        return 7;
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
        return 8;
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
        return 9;
    }

    morph_runtime_module_destroy(&module, &host);
    puts("PASS: transactional activation, migration, and checkpoint restoration");
    return 0;
}
