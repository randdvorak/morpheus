#include "authoring_shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

static void set_text(char *destination, unsigned long capacity, const char *text)
{
    if (!destination || !capacity) return;
    snprintf(destination, (size_t)capacity, "%s", text ? text : "");
}

static int preserve_bootstrap_state(
    morph_authoring_shell *shell,
    char *error,
    unsigned long error_capacity)
{
    const void *state = NULL;
    unsigned long state_size = 0;
    if (!morph_runtime_module_capture_state(
            &shell->module,
            shell->host,
            &state,
            &state_size,
            error,
            error_capacity)) return 0;
    if (state_size) {
        shell->bootstrap_state = malloc((size_t)state_size);
        if (!shell->bootstrap_state) {
            set_text(error, error_capacity,
                "Unable to preserve bootstrap authoring UI state");
            return 0;
        }
        memcpy(shell->bootstrap_state, state, (size_t)state_size);
    }
    shell->bootstrap_state_size = state_size;
    return 1;
}

static int path_exists(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    fclose(file);
    return 1;
}

static int write_atomic(
    const char *path,
    const void *data,
    unsigned long size,
    char *error,
    unsigned long error_capacity)
{
    char temporary[MORPHEUS_AUTHORING_SHELL_PATH_CAPACITY];
    FILE *file;
    int failed = 0;
    int length = snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    if (length < 0 || (unsigned long)length >= sizeof(temporary)) {
        set_text(error, error_capacity, "Authoring recovery path is too long");
        return 0;
    }
    file = fopen(temporary, "wb");
    if (!file) {
        set_text(error, error_capacity, strerror(errno));
        return 0;
    }
    if (size && fwrite(data, 1, (size_t)size, file) != (size_t)size) failed = 1;
    if (fflush(file) != 0) failed = 1;
    if (fclose(file) != 0) failed = 1;
    if (failed) {
        remove(temporary);
        set_text(error, error_capacity, "Unable to write authoring recovery data");
        return 0;
    }
    if (rename(temporary, path) != 0) {
        remove(temporary);
        set_text(error, error_capacity, strerror(errno));
        return 0;
    }
    return 1;
}

static int copy_file_atomic(
    const char *source_path,
    const char *destination_path,
    char *error,
    unsigned long error_capacity)
{
    FILE *source;
    unsigned char *data;
    long length;
    size_t read_size;
    int result;
    source = fopen(source_path, "rb");
    if (!source) {
        set_text(error, error_capacity, strerror(errno));
        return 0;
    }
    if (fseek(source, 0, SEEK_END) != 0 ||
        (length = ftell(source)) < 0 ||
        length > 4 * 1024 * 1024 ||
        fseek(source, 0, SEEK_SET) != 0) {
        fclose(source);
        set_text(error, error_capacity, "Authoring source is invalid or too large");
        return 0;
    }
    data = length ? malloc((size_t)length) : NULL;
    if (length && !data) {
        fclose(source);
        set_text(error, error_capacity, "Unable to copy accepted authoring source");
        return 0;
    }
    read_size = length ? fread(data, 1, (size_t)length, source) : 0;
    if ((length && read_size != (size_t)length) || fclose(source) != 0) {
        free(data);
        set_text(error, error_capacity, "Unable to read accepted authoring source");
        return 0;
    }
    result = write_atomic(destination_path, data, (unsigned long)length,
        error, error_capacity);
    free(data);
    return result;
}

static int arm_session(
    morph_authoring_shell *shell,
    char *error,
    unsigned long error_capacity)
{
    static const char marker[] = "active\n";
    return write_atomic(shell->session_path, marker, sizeof(marker) - 1,
        error, error_capacity);
}

int morph_authoring_shell_init(
    morph_authoring_shell *shell,
    morph_host *host,
    const morph_app_api *bootstrap_api,
    const char *source_path,
    const char *storage_root,
    int safe_mode,
    char *error,
    unsigned long error_capacity)
{
    int accepted_exists;
    if (!shell || !host || !bootstrap_api || !source_path || !*source_path ||
        !storage_root || !*storage_root) {
        set_text(error, error_capacity, "Invalid authoring shell configuration");
        return 0;
    }
    memset(shell, 0, sizeof(*shell));
    shell->host = host;
    shell->bootstrap_api = bootstrap_api;
    shell->safe_mode = safe_mode != 0;
    set_text(shell->source_path, sizeof(shell->source_path), source_path);
    set_text(shell->storage_root, sizeof(shell->storage_root), storage_root);
    if (mkdir(shell->storage_root, 0755) != 0 && errno != EEXIST) {
        set_text(error, error_capacity, strerror(errno));
        memset(shell, 0, sizeof(*shell));
        return 0;
    }
    if (snprintf(shell->accepted_source_path,
            sizeof(shell->accepted_source_path), "%s/accepted.c",
            shell->storage_root) >= (int)sizeof(shell->accepted_source_path) ||
        snprintf(shell->session_path,
            sizeof(shell->session_path), "%s/session.active",
            shell->storage_root) >= (int)sizeof(shell->session_path)) {
        set_text(error, error_capacity, "Authoring recovery path is too long");
        memset(shell, 0, sizeof(*shell));
        return 0;
    }
    shell->recovered_from_crash = path_exists(shell->session_path);
    accepted_exists = path_exists(shell->accepted_source_path);
    morph_runtime_module_init(&shell->module);
    if (!morph_runtime_module_bootstrap(
            &shell->module,
            shell->host,
            shell->bootstrap_api,
            MORPHEUS_RENDER_EMBEDDED,
            error,
            error_capacity) ||
        !preserve_bootstrap_state(shell, error, error_capacity)) {
        morph_runtime_module_destroy(&shell->module, shell->host);
        free(shell->bootstrap_state);
        memset(shell, 0, sizeof(*shell));
        return 0;
    }
    shell->state = MORPHEUS_AUTHORING_SHELL_BOOTSTRAP;
    if (!shell->safe_mode && !shell->recovered_from_crash && accepted_exists &&
        morph_runtime_module_compile_candidate(
            &shell->module,
            shell->accepted_source_path,
            error,
            error_capacity) &&
        morph_runtime_module_activate_candidate_with_state(
            &shell->module,
            shell->host,
            shell->bootstrap_state,
            shell->bootstrap_state_size,
            error,
            error_capacity)) {
        shell->state = MORPHEUS_AUTHORING_SHELL_ACCEPTED;
        set_text(shell->message, sizeof(shell->message),
            "Accepted authoring UI restored");
    } else if (shell->safe_mode) {
        set_text(shell->message, sizeof(shell->message),
            "Authoring safe mode: known-good bootstrap active");
    } else if (shell->recovered_from_crash) {
        set_text(shell->message, sizeof(shell->message),
            "Authoring crash recovery: known-good bootstrap active");
    } else if (accepted_exists && error && *error) {
        set_text(shell->message, sizeof(shell->message), error);
    } else {
        set_text(shell->message, sizeof(shell->message),
            "Known-good authoring UI active");
    }
    if (!arm_session(shell, error, error_capacity)) {
        morph_runtime_module_destroy(&shell->module, shell->host);
        free(shell->bootstrap_state);
        memset(shell, 0, sizeof(*shell));
        return 0;
    }
    return 1;
}

int morph_authoring_shell_snapshot_get(
    const morph_authoring_shell *shell,
    morph_authoring_shell_snapshot *snapshot)
{
    unsigned long struct_size;
    if (!shell || !snapshot || snapshot->struct_size < sizeof(*snapshot)) return 0;
    struct_size = snapshot->struct_size;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->struct_size = struct_size;
    snapshot->state = shell->state;
    snapshot->can_preview = shell->module.api != NULL;
    snapshot->can_accept = shell->state == MORPHEUS_AUTHORING_SHELL_PREVIEW;
    snapshot->can_rollback = shell->state != MORPHEUS_AUTHORING_SHELL_BOOTSTRAP;
    snapshot->safe_mode = shell->safe_mode;
    snapshot->recovered_from_crash = shell->recovered_from_crash;
    set_text(snapshot->message, sizeof(snapshot->message), shell->message);
    return 1;
}

int morph_authoring_shell_preview(
    morph_authoring_shell *shell,
    char *error,
    unsigned long error_capacity)
{
    if (!shell || !shell->host) return 0;
    if (!morph_runtime_module_compile_candidate(
            &shell->module,
            shell->source_path,
            error,
            error_capacity) ||
        !morph_runtime_module_activate_candidate_with_state(
            &shell->module,
            shell->host,
            shell->bootstrap_state,
            shell->bootstrap_state_size,
            error,
            error_capacity)) {
        set_text(shell->message, sizeof(shell->message),
            error && *error ? error : "Authoring UI preview failed");
        return 0;
    }
    shell->state = MORPHEUS_AUTHORING_SHELL_PREVIEW;
    set_text(shell->message, sizeof(shell->message),
        "Authoring UI preview active — accept or roll back");
    return 1;
}

int morph_authoring_shell_accept(
    morph_authoring_shell *shell,
    char *error,
    unsigned long error_capacity)
{
    if (!shell || shell->state != MORPHEUS_AUTHORING_SHELL_PREVIEW) {
        set_text(error, error_capacity, "No authoring UI preview is active");
        return 0;
    }
    if (!copy_file_atomic(
            shell->source_path,
            shell->accepted_source_path,
            error,
            error_capacity)) {
        set_text(shell->message, sizeof(shell->message),
            error && *error ? error : "Unable to persist accepted authoring UI");
        return 0;
    }
    shell->state = MORPHEUS_AUTHORING_SHELL_ACCEPTED;
    set_text(shell->message, sizeof(shell->message),
        "Authoring UI candidate accepted for this session");
    if (error && error_capacity) error[0] = '\0';
    return 1;
}

int morph_authoring_shell_rollback(
    morph_authoring_shell *shell,
    char *error,
    unsigned long error_capacity)
{
    if (!shell || !shell->host ||
        shell->state == MORPHEUS_AUTHORING_SHELL_BOOTSTRAP) {
        set_text(error, error_capacity, "Authoring UI is already using the bootstrap");
        return 0;
    }
    if (!morph_runtime_module_stage_static_candidate(
            &shell->module,
            shell->bootstrap_api,
            MORPHEUS_RENDER_EMBEDDED,
            error,
            error_capacity) ||
        !morph_runtime_module_activate_candidate_with_state(
            &shell->module,
            shell->host,
            shell->bootstrap_state,
            shell->bootstrap_state_size,
            error,
            error_capacity)) {
        set_text(shell->message, sizeof(shell->message),
            error && *error ? error : "Authoring UI rollback failed");
        return 0;
    }
    shell->state = MORPHEUS_AUTHORING_SHELL_BOOTSTRAP;
    set_text(shell->message, sizeof(shell->message),
        "Rolled back to known-good authoring UI");
    return 1;
}

void morph_authoring_shell_update(morph_authoring_shell *shell, double dt)
{
    if (shell && shell->host) {
        morph_runtime_module_update(&shell->module, shell->host, dt);
    }
}

void morph_authoring_shell_render(morph_authoring_shell *shell)
{
    if (shell && shell->host) {
        morph_runtime_module_render_ui(&shell->module, shell->host);
    }
}

void morph_authoring_shell_destroy(morph_authoring_shell *shell)
{
    if (!shell) return;
    if (shell->host) {
        morph_runtime_module_destroy(&shell->module, shell->host);
    }
    if (shell->session_path[0]) (void)remove(shell->session_path);
    free(shell->bootstrap_state);
    memset(shell, 0, sizeof(*shell));
}
