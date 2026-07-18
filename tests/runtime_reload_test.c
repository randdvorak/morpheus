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

    morph_runtime_module_init(&module);

    if (!morph_runtime_module_reload(
            &module,
            &host,
            MORPHEUS_TEST_FIXTURE_ROOT "/module_v1.c",
            error,
            sizeof(error)) ||
        !expect_active(&module, &host, "version-one", "version one")) {
        fprintf(stderr, "initial load failed: %s\n", error);
        return 1;
    }
    version_one_compiler = module.compiler;

    if (morph_runtime_module_reload(
            &module,
            &host,
            MORPHEUS_TEST_FIXTURE_ROOT "/module_invalid.c",
            error,
            sizeof(error)) ||
        !expect_active(&module, &host, "version-one", "version one")) {
        fprintf(stderr, "invalid candidate replaced the active module\n");
        return 2;
    }

    if (!morph_runtime_module_reload(
            &module,
            &host,
            MORPHEUS_TEST_FIXTURE_ROOT "/module_v2.c",
            error,
            sizeof(error)) ||
        module.compiler == version_one_compiler ||
        !expect_active(&module, &host, "version-two", "version two")) {
        fprintf(stderr, "valid reload failed: %s\n", error);
        return 3;
    }

    morph_runtime_module_destroy(&module, &host);
    puts("PASS: valid reload and invalid-candidate rollback");
    return 0;
}
