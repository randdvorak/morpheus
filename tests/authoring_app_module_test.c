#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_IMPLEMENTATION
#include "nuklear.h"

#include "agent_session.h"
#include "authoring_capabilities.h"
#include "authoring_controller.h"
#include "authoring_shell.h"
#include "export_service.h"
#include "morpheus/authoring_app.h"
#include "project_store.h"
#include "revision_store.h"
#include "runtime_module.h"

static char rendered_label[128];
static int saw_module_label;
static int saw_revision_label;
static int saw_recompile_button;

static void capture_label(morph_host *host, const char *text)
{
    (void)host;
    snprintf(rendered_label, sizeof(rendered_label), "%s", text ? text : "");
    if (text && strcmp(text, "Morpheus authoring UI module: active") == 0) {
        saw_module_label = 1;
    }
    if (text && strcmp(text, "Accepted revision: 0") == 0) {
        saw_revision_label = 1;
    }
}

static int capture_button(morph_host *host, const char *text)
{
    (void)host;
    if (text && strcmp(text, "Recompile current app") == 0) {
        saw_recompile_button = 1;
        return 1;
    }
    return 0;
}

int main(void)
{
    morph_project_store projects = {0};
    morph_revision_store revisions = {0};
    morph_runtime_module module = {0};
    morph_agent_session agent = {0};
    morph_export_service export_service = {0};
    morph_authoring_controller controller = {0};
    morph_capability entries[6];
    morph_capability_registry registry;
    morph_host host = {0};
    const morph_app_api *app;
    morph_authoring_shell shell;
    morph_authoring_shell_snapshot shell_snapshot = {
        .struct_size = sizeof(shell_snapshot)
    };
    char shell_root[] = "/tmp/morpheus-authoring-app-XXXXXX";
    void *state = NULL;
    const void *saved = (const void *)1;
    unsigned long saved_size = 1;

    entries[0] = morph_authoring_projects_capability(&projects);
    entries[1] = morph_authoring_revisions_capability(&revisions);
    entries[2] = morph_authoring_modules_capability(&module);
    entries[3] = morph_authoring_agent_capability(&agent);
    entries[4] = morph_authoring_export_capability(
        &export_service, "/usr/bin/true");
    registry.entries = entries;
    registry.count = 5;
    host.abi_version = MORPHEUS_HOST_ABI_VERSION;
    host.capabilities = &registry;
    host.ui_label = capture_label;
    host.ui_button = capture_button;
    if (!morph_authoring_controller_init(
            &controller, &registry, &host)) {
        fprintf(stderr, "authoring controller initialization failed\n");
        return 1;
    }
    entries[5] = morph_authoring_controller_capability(&controller);
    registry.count = sizeof(entries) / sizeof(entries[0]);
    snprintf(controller.source_path, sizeof(controller.source_path),
        "generated/app.c");
    morph_authoring_controller_set_availability(&controller, 0, 1);

    app = morph_authoring_app_entry();
    if (!app || app->abi_version != MORPHEUS_APP_ABI_VERSION ||
        strcmp(app->name, "Morpheus Authoring UI") != 0 ||
        !app->create(&host, &state) || !state) {
        fprintf(stderr, "authoring app initialization failed\n");
        return 1;
    }
    app->update(&host, state, 0.016);
    app->render_ui(&host, state);
    if (!saw_module_label || !saw_revision_label || !saw_recompile_button ||
        !morph_authoring_controller_has_pending(&controller) ||
        strcmp(rendered_label, "Authoring ready") != 0 ||
        !app->save_state(&host, state, &saved, &saved_size) ||
        saved != NULL || saved_size != 0 ||
        !app->load_state(&host, &state, NULL, 0)) {
        fprintf(stderr, "authoring app lifecycle failed\n");
        app->destroy(&host, state);
        return 2;
    }
    app->destroy(&host, state);
    state = NULL;
    if (!mkdtemp(shell_root) || !morph_authoring_shell_init(
            &shell,
            &host,
            app,
            MORPHEUS_TEST_AUTHORING_UI_SOURCE,
            shell_root,
            0,
            rendered_label,
            sizeof(rendered_label)) ||
        !morph_authoring_shell_preview(
            &shell, rendered_label, sizeof(rendered_label)) ||
        !morph_authoring_shell_snapshot_get(&shell, &shell_snapshot) ||
        shell_snapshot.state != MORPHEUS_AUTHORING_SHELL_PREVIEW ||
        !morph_authoring_shell_rollback(
            &shell, rendered_label, sizeof(rendered_label))) {
        fprintf(stderr, "real authoring source reload failed: %s\n", rendered_label);
        morph_authoring_shell_destroy(&shell);
        return 3;
    }
    morph_authoring_shell_destroy(&shell);
    (void)rmdir(shell_root);
    puts("PASS: Morpheus authoring UI runs through morph_app_api");
    return 0;
}
