#ifndef MORPHEUS_AUTHORING_SHELL_H
#define MORPHEUS_AUTHORING_SHELL_H

#include "morpheus/app_api.h"
#include "runtime_module.h"

#define MORPHEUS_AUTHORING_SHELL_MESSAGE_CAPACITY 512
#define MORPHEUS_AUTHORING_SHELL_PATH_CAPACITY 4096

typedef enum morph_authoring_shell_state {
    MORPHEUS_AUTHORING_SHELL_BOOTSTRAP = 0,
    MORPHEUS_AUTHORING_SHELL_PREVIEW,
    MORPHEUS_AUTHORING_SHELL_ACCEPTED
} morph_authoring_shell_state;

typedef struct morph_authoring_shell_snapshot {
    unsigned long struct_size;
    morph_authoring_shell_state state;
    int can_preview;
    int can_accept;
    int can_rollback;
    int safe_mode;
    int recovered_from_crash;
    char message[MORPHEUS_AUTHORING_SHELL_MESSAGE_CAPACITY];
} morph_authoring_shell_snapshot;

typedef struct morph_authoring_shell {
    morph_runtime_module module;
    morph_host *host;
    const morph_app_api *bootstrap_api;
    morph_authoring_shell_state state;
    int safe_mode;
    int recovered_from_crash;
    void *bootstrap_state;
    unsigned long bootstrap_state_size;
    char source_path[MORPHEUS_AUTHORING_SHELL_PATH_CAPACITY];
    char storage_root[MORPHEUS_AUTHORING_SHELL_PATH_CAPACITY];
    char accepted_source_path[MORPHEUS_AUTHORING_SHELL_PATH_CAPACITY];
    char session_path[MORPHEUS_AUTHORING_SHELL_PATH_CAPACITY];
    char message[MORPHEUS_AUTHORING_SHELL_MESSAGE_CAPACITY];
} morph_authoring_shell;

int morph_authoring_shell_init(
    morph_authoring_shell *shell,
    morph_host *host,
    const morph_app_api *bootstrap_api,
    const char *source_path,
    const char *storage_root,
    int safe_mode,
    char *error,
    unsigned long error_capacity);
int morph_authoring_shell_snapshot_get(
    const morph_authoring_shell *shell,
    morph_authoring_shell_snapshot *snapshot);
int morph_authoring_shell_preview(
    morph_authoring_shell *shell,
    char *error,
    unsigned long error_capacity);
int morph_authoring_shell_accept(
    morph_authoring_shell *shell,
    char *error,
    unsigned long error_capacity);
int morph_authoring_shell_rollback(
    morph_authoring_shell *shell,
    char *error,
    unsigned long error_capacity);
void morph_authoring_shell_update(morph_authoring_shell *shell, double dt);
void morph_authoring_shell_render(morph_authoring_shell *shell);
void morph_authoring_shell_destroy(morph_authoring_shell *shell);

#endif
