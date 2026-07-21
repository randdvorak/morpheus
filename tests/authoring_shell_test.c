#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "authoring_shell.h"

static int bootstrap_creates;
static int bootstrap_destroys;
static char rendered[128];

static int bootstrap_create(morph_host *host, void **state)
{
    (void)host;
    bootstrap_creates++;
    *state = &bootstrap_creates;
    return 1;
}

static void bootstrap_destroy(morph_host *host, void *state)
{
    (void)host;
    (void)state;
    bootstrap_destroys++;
}

static void bootstrap_render(morph_host *host, void *state)
{
    (void)state;
    host->ui_label(host, "bootstrap");
}

static int bootstrap_save(
    morph_host *host,
    void *state,
    const void **data,
    unsigned long *size)
{
    (void)host;
    (void)state;
    *data = NULL;
    *size = 0;
    return 1;
}

static int bootstrap_load(
    morph_host *host,
    void **state,
    const void *data,
    unsigned long size)
{
    (void)host;
    (void)state;
    return !data && size == 0;
}

static const morph_app_api bootstrap_api = {
    MORPHEUS_APP_ABI_VERSION,
    "bootstrap",
    bootstrap_create,
    bootstrap_destroy,
    NULL,
    bootstrap_render,
    bootstrap_save,
    bootstrap_load
};

static void capture_label(morph_host *host, const char *text)
{
    (void)host;
    snprintf(rendered, sizeof(rendered), "%s", text ? text : "");
}

int main(void)
{
    morph_host host = {
        .abi_version = MORPHEUS_HOST_ABI_VERSION,
        .ui_label = capture_label
    };
    morph_authoring_shell shell;
    morph_authoring_shell_snapshot snapshot = {.struct_size = sizeof(snapshot)};
    char error[512];
    char root[] = "/tmp/morpheus-authoring-shell-XXXXXX";
    char accepted_path[512];

    if (!mkdtemp(root) || !morph_authoring_shell_init(
            &shell,
            &host,
            &bootstrap_api,
            MORPHEUS_TEST_AUTHORING_CANDIDATE,
            root,
            0,
            error,
            sizeof(error))) return 1;
    morph_authoring_shell_render(&shell);
    if (strcmp(rendered, "bootstrap") != 0 || bootstrap_creates != 1 ||
        !morph_authoring_shell_snapshot_get(&shell, &snapshot) ||
        snapshot.state != MORPHEUS_AUTHORING_SHELL_BOOTSTRAP ||
        !snapshot.can_preview || snapshot.can_accept || snapshot.can_rollback) return 2;

    snprintf(shell.source_path, sizeof(shell.source_path), "%s",
        MORPHEUS_TEST_INVALID_AUTHORING_CANDIDATE);
    if (morph_authoring_shell_preview(&shell, error, sizeof(error)) ||
        shell.state != MORPHEUS_AUTHORING_SHELL_BOOTSTRAP) return 3;
    morph_authoring_shell_render(&shell);
    if (strcmp(rendered, "bootstrap") != 0) return 4;

    snprintf(shell.source_path, sizeof(shell.source_path), "%s",
        MORPHEUS_TEST_AUTHORING_CANDIDATE);
    if (!morph_authoring_shell_preview(&shell, error, sizeof(error)) ||
        shell.state != MORPHEUS_AUTHORING_SHELL_PREVIEW) return 5;
    morph_authoring_shell_render(&shell);
    if (strcmp(rendered, "version one: 41") != 0 || bootstrap_destroys != 1)
        return 6;
    if (!morph_authoring_shell_accept(&shell, error, sizeof(error)) ||
        shell.state != MORPHEUS_AUTHORING_SHELL_ACCEPTED) return 7;
    if (!morph_authoring_shell_rollback(&shell, error, sizeof(error)) ||
        shell.state != MORPHEUS_AUTHORING_SHELL_BOOTSTRAP) return 8;
    morph_authoring_shell_render(&shell);
    if (strcmp(rendered, "bootstrap") != 0 || bootstrap_creates != 2) return 9;

    morph_authoring_shell_destroy(&shell);
    if (bootstrap_destroys != 2) return 10;

    if (!morph_authoring_shell_init(
            &shell,
            &host,
            &bootstrap_api,
            MORPHEUS_TEST_AUTHORING_CANDIDATE,
            root,
            0,
            error,
            sizeof(error)) ||
        shell.state != MORPHEUS_AUTHORING_SHELL_ACCEPTED) return 11;
    morph_authoring_shell_render(&shell);
    if (strcmp(rendered, "version one: 41") != 0) return 12;

    /* Simulate an abnormal process exit: release executable memory but leave
       the armed session marker behind. */
    morph_runtime_module_destroy(&shell.module, &host);
    free(shell.bootstrap_state);
    memset(&shell, 0, sizeof(shell));
    if (!morph_authoring_shell_init(
            &shell,
            &host,
            &bootstrap_api,
            MORPHEUS_TEST_AUTHORING_CANDIDATE,
            root,
            0,
            error,
            sizeof(error)) ||
        shell.state != MORPHEUS_AUTHORING_SHELL_BOOTSTRAP ||
        !shell.recovered_from_crash) return 13;
    morph_authoring_shell_destroy(&shell);

    if (!morph_authoring_shell_init(
            &shell,
            &host,
            &bootstrap_api,
            MORPHEUS_TEST_AUTHORING_CANDIDATE,
            root,
            1,
            error,
            sizeof(error)) ||
        shell.state != MORPHEUS_AUTHORING_SHELL_BOOTSTRAP ||
        !shell.safe_mode) return 14;
    morph_authoring_shell_destroy(&shell);
    snprintf(accepted_path, sizeof(accepted_path), "%s/accepted.c", root);

    {
        FILE *corrupt = fopen(accepted_path, "wb");
        static const char invalid[] = "this is not C source\n";
        if (!corrupt || fwrite(invalid, 1, sizeof(invalid) - 1, corrupt) !=
                sizeof(invalid) - 1 || fclose(corrupt) != 0) return 15;
    }
    if (!morph_authoring_shell_init(
            &shell,
            &host,
            &bootstrap_api,
            MORPHEUS_TEST_AUTHORING_CANDIDATE,
            root,
            0,
            error,
            sizeof(error)) ||
        shell.state != MORPHEUS_AUTHORING_SHELL_BOOTSTRAP) return 16;
    morph_authoring_shell_render(&shell);
    if (strcmp(rendered, "bootstrap") != 0) return 17;
    snprintf(shell.source_path, sizeof(shell.source_path), "%s",
        MORPHEUS_TEST_INIT_FAILURE_AUTHORING_CANDIDATE);
    if (morph_authoring_shell_preview(&shell, error, sizeof(error)) ||
        shell.state != MORPHEUS_AUTHORING_SHELL_BOOTSTRAP) return 18;
    morph_authoring_shell_render(&shell);
    if (strcmp(rendered, "bootstrap") != 0) return 19;
    morph_authoring_shell_destroy(&shell);

    (void)remove(accepted_path);
    (void)rmdir(root);
    puts("PASS: authoring shell previews, accepts, and rolls back through runtime loader");
    return 0;
}
