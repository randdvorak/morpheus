#ifndef MORPHEUS_AUTHORING_H
#define MORPHEUS_AUTHORING_H

#include "morpheus/app_api.h"

#define MORPHEUS_AUTHORING_PROJECTS_CAPABILITY \
    "dev.morpheus.authoring.projects"
#define MORPHEUS_AUTHORING_PROJECTS_ABI_VERSION 1u

#define MORPHEUS_AUTHORING_MAX_PROJECTS 32
#define MORPHEUS_AUTHORING_PROJECT_NAME_CAPACITY 64
#define MORPHEUS_AUTHORING_PROJECT_SLUG_CAPACITY 64

#define MORPHEUS_AUTHORING_REVISIONS_CAPABILITY \
    "dev.morpheus.authoring.revisions"
#define MORPHEUS_AUTHORING_REVISIONS_ABI_VERSION 1u
#define MORPHEUS_AUTHORING_REVISION_PATH_CAPACITY 4096

#define MORPHEUS_AUTHORING_MODULES_CAPABILITY \
    "dev.morpheus.authoring.modules"
#define MORPHEUS_AUTHORING_MODULES_ABI_VERSION 1u

#define MORPHEUS_AUTHORING_AGENT_CAPABILITY \
    "dev.morpheus.authoring.agent"
#define MORPHEUS_AUTHORING_AGENT_ABI_VERSION 1u
#define MORPHEUS_AUTHORING_AGENT_PATH_CAPACITY 4096
#define MORPHEUS_AUTHORING_AGENT_REQUEST_CAPACITY 2048
#define MORPHEUS_AUTHORING_AGENT_MODEL_CAPACITY 256
#define MORPHEUS_AUTHORING_AGENT_MAX_ATTEMPTS 3

#define MORPHEUS_AUTHORING_EXPORT_CAPABILITY \
    "dev.morpheus.authoring.export"
#define MORPHEUS_AUTHORING_EXPORT_ABI_VERSION 1u
#define MORPHEUS_AUTHORING_EXPORT_PATH_CAPACITY 4096

#define MORPHEUS_AUTHORING_CONTROLLER_CAPABILITY \
    "dev.morpheus.authoring.controller"
#define MORPHEUS_AUTHORING_CONTROLLER_ABI_VERSION 1u
#define MORPHEUS_AUTHORING_CONTROLLER_NAME_CAPACITY 128
#define MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY 512
#define MORPHEUS_AUTHORING_CONTROLLER_PATH_CAPACITY 4096

typedef enum morph_authoring_export_status {
    MORPHEUS_AUTHORING_EXPORT_IDLE = 0,
    MORPHEUS_AUTHORING_EXPORT_RUNNING,
    MORPHEUS_AUTHORING_EXPORT_SUCCEEDED,
    MORPHEUS_AUTHORING_EXPORT_FAILED
} morph_authoring_export_status;

typedef struct morph_authoring_project_info {
    char name[MORPHEUS_AUTHORING_PROJECT_NAME_CAPACITY];
    char slug[MORPHEUS_AUTHORING_PROJECT_SLUG_CAPACITY];
} morph_authoring_project_info;

typedef enum morph_authoring_command {
    MORPHEUS_AUTHORING_COMMAND_NONE = 0,
    MORPHEUS_AUTHORING_COMMAND_RECOMPILE,
    MORPHEUS_AUTHORING_COMMAND_ROLLBACK,
    MORPHEUS_AUTHORING_COMMAND_CREATE_PROJECT,
    MORPHEUS_AUTHORING_COMMAND_SELECT_PROJECT,
    MORPHEUS_AUTHORING_COMMAND_TOGGLE_AGENT_PROVIDER,
    MORPHEUS_AUTHORING_COMMAND_SUBMIT_AGENT,
    MORPHEUS_AUTHORING_COMMAND_ACCEPT_PREVIEW,
    MORPHEUS_AUTHORING_COMMAND_REJECT_PREVIEW,
    MORPHEUS_AUTHORING_COMMAND_START_EXPORT,
    MORPHEUS_AUTHORING_COMMAND_CANCEL_EXPORT
} morph_authoring_command;

typedef struct morph_authoring_request {
    unsigned long struct_size;
    morph_authoring_command command;
    unsigned int project_index;
    char text[MORPHEUS_AUTHORING_AGENT_REQUEST_CAPACITY];
    char model[MORPHEUS_AUTHORING_AGENT_MODEL_CAPACITY];
} morph_authoring_request;

typedef struct morph_authoring_snapshot {
    unsigned long struct_size;
    int runtime_active;
    int commands_enabled;
    int can_recompile;
    int can_rollback;
    int command_pending;
    int projects_enabled;
    unsigned int project_count;
    unsigned int active_project_index;
    morph_authoring_project_info projects[MORPHEUS_AUTHORING_MAX_PROJECTS];
    int recovered_from_crash;
    int agent_ready;
    int agent_running;
    int agent_preview_active;
    int agent_provider_is_custom;
    int agent_uses_ollama;
    int can_change_project;
    int can_toggle_agent_provider;
    int can_submit_agent;
    int can_accept_preview;
    int can_reject_preview;
    int can_start_export;
    int can_cancel_export;
    morph_authoring_export_status export_status;
    unsigned long active_revision;
    char active_name[MORPHEUS_AUTHORING_CONTROLLER_NAME_CAPACITY];
    char agent_status[MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY];
    char export_status_text[MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY];
    char diagnostics[MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY];
    char message[MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY];
} morph_authoring_snapshot;

typedef struct morph_authoring_controller_api {
    unsigned int abi_version;
    unsigned long struct_size;
    int (*snapshot)(void *context, morph_authoring_snapshot *snapshot);
    int (*dispatch)(
        void *context,
        const morph_authoring_request *request,
        char *error,
        unsigned long error_capacity);
} morph_authoring_controller_api;

static inline const morph_authoring_controller_api *
morph_authoring_controller_from_capability(const morph_capability *capability)
{
    const morph_authoring_controller_api *api;
    if (!capability ||
        capability->abi_version < MORPHEUS_AUTHORING_CONTROLLER_ABI_VERSION ||
        capability->api_size < sizeof(morph_authoring_controller_api) ||
        !capability->api) return NULL;
    api = (const morph_authoring_controller_api *)capability->api;
    if (api->abi_version < MORPHEUS_AUTHORING_CONTROLLER_ABI_VERSION ||
        api->struct_size < sizeof(morph_authoring_controller_api) ||
        !api->snapshot || !api->dispatch) return NULL;
    return api;
}

typedef enum morph_authoring_agent_status {
    MORPHEUS_AUTHORING_AGENT_IDLE = 0,
    MORPHEUS_AUTHORING_AGENT_RUNNING,
    MORPHEUS_AUTHORING_AGENT_PROVIDER_SUCCEEDED,
    MORPHEUS_AUTHORING_AGENT_PROVIDER_FAILED
} morph_authoring_agent_status;

typedef enum morph_authoring_module_stage {
    MORPHEUS_AUTHORING_MODULE_IDLE = 0,
    MORPHEUS_AUTHORING_MODULE_COMPILE,
    MORPHEUS_AUTHORING_MODULE_VALIDATE,
    MORPHEUS_AUTHORING_MODULE_SAVE_STATE,
    MORPHEUS_AUTHORING_MODULE_INITIALIZE,
    MORPHEUS_AUTHORING_MODULE_MIGRATE,
    MORPHEUS_AUTHORING_MODULE_ACTIVE
} morph_authoring_module_stage;

typedef struct morph_authoring_projects_api {
    unsigned int abi_version;
    unsigned long struct_size;
    int (*init)(
        void *context,
        const char *projects_root,
        char *error,
        unsigned long error_capacity);
    unsigned int (*count)(void *context);
    unsigned int (*active_index)(void *context);
    int (*project)(
        void *context,
        unsigned int index,
        morph_authoring_project_info *project);
    int (*create)(
        void *context,
        const char *name,
        char *error,
        unsigned long error_capacity);
    int (*select)(
        void *context,
        unsigned int index,
        char *error,
        unsigned long error_capacity);
    int (*paths)(
        void *context,
        char *workspace,
        unsigned long workspace_capacity,
        char *source,
        unsigned long source_capacity,
        char *assets,
        unsigned long assets_capacity);
} morph_authoring_projects_api;

static inline const morph_authoring_projects_api *
morph_authoring_projects_from_capability(const morph_capability *capability)
{
    const morph_authoring_projects_api *api;
    if (!capability ||
        capability->abi_version < MORPHEUS_AUTHORING_PROJECTS_ABI_VERSION ||
        capability->api_size < sizeof(morph_authoring_projects_api) ||
        !capability->api) return NULL;
    api = (const morph_authoring_projects_api *)capability->api;
    if (api->abi_version < MORPHEUS_AUTHORING_PROJECTS_ABI_VERSION ||
        api->struct_size < sizeof(morph_authoring_projects_api) ||
        !api->init || !api->count || !api->active_index || !api->project ||
        !api->create || !api->select || !api->paths) return NULL;
    return api;
}

typedef struct morph_authoring_revisions_api {
    unsigned int abi_version;
    unsigned long struct_size;
    int (*init)(
        void *context,
        const char *workspace_root,
        char *error,
        unsigned long error_capacity);
    unsigned long (*active_revision)(void *context);
    unsigned long (*latest_revision)(void *context);
    int (*checkpoint)(
        void *context,
        const char *source_path,
        const void *state_data,
        unsigned long state_size,
        unsigned long *revision,
        char *error,
        unsigned long error_capacity);
    int (*load)(
        void *context,
        unsigned long revision,
        char *source_path,
        unsigned long source_path_capacity,
        void **state_data,
        unsigned long *state_size,
        char *error,
        unsigned long error_capacity);
    void (*release_state)(void *context, void *state_data);
    int (*previous)(void *context, unsigned long *revision);
    int (*set_active)(
        void *context,
        unsigned long revision,
        char *error,
        unsigned long error_capacity);
    int (*begin_session)(
        void *context,
        int *recovered_from_crash,
        char *error,
        unsigned long error_capacity);
    int (*refresh_session)(
        void *context,
        char *error,
        unsigned long error_capacity);
    int (*end_session)(
        void *context,
        char *error,
        unsigned long error_capacity);
    int (*record_attempt)(
        void *context,
        const char *stage,
        int succeeded,
        const char *message,
        char *error,
        unsigned long error_capacity);
} morph_authoring_revisions_api;

static inline const morph_authoring_revisions_api *
morph_authoring_revisions_from_capability(const morph_capability *capability)
{
    const morph_authoring_revisions_api *api;
    if (!capability ||
        capability->abi_version < MORPHEUS_AUTHORING_REVISIONS_ABI_VERSION ||
        capability->api_size < sizeof(morph_authoring_revisions_api) ||
        !capability->api) return NULL;
    api = (const morph_authoring_revisions_api *)capability->api;
    if (api->abi_version < MORPHEUS_AUTHORING_REVISIONS_ABI_VERSION ||
        api->struct_size < sizeof(morph_authoring_revisions_api) ||
        !api->init || !api->active_revision || !api->latest_revision ||
        !api->checkpoint || !api->load || !api->release_state ||
        !api->previous || !api->set_active || !api->begin_session ||
        !api->refresh_session || !api->end_session ||
        !api->record_attempt) return NULL;
    return api;
}

typedef struct morph_authoring_modules_api {
    unsigned int abi_version;
    unsigned long struct_size;
    void (*init)(void *context);
    int (*compile_candidate)(
        void *context,
        const char *source_path,
        char *error,
        unsigned long error_capacity);
    int (*activate_candidate)(
        void *context,
        morph_host *host,
        char *error,
        unsigned long error_capacity);
    int (*activate_candidate_with_state)(
        void *context,
        morph_host *host,
        const void *state_data,
        unsigned long state_size,
        char *error,
        unsigned long error_capacity);
    int (*capture_state)(
        void *context,
        morph_host *host,
        const void **state_data,
        unsigned long *state_size,
        char *error,
        unsigned long error_capacity);
    int (*has_candidate)(void *context);
    int (*is_active)(void *context);
    const char *(*active_name)(void *context);
    unsigned int (*render_mode)(void *context);
    morph_authoring_module_stage (*last_stage)(void *context);
    const char *(*stage_name)(
        void *context,
        morph_authoring_module_stage stage);
    int (*reload)(
        void *context,
        morph_host *host,
        const char *source_path,
        char *error,
        unsigned long error_capacity);
    void (*update)(void *context, morph_host *host, double dt);
    void (*render_ui)(void *context, morph_host *host);
    void (*destroy)(void *context, morph_host *host);
} morph_authoring_modules_api;

static inline const morph_authoring_modules_api *
morph_authoring_modules_from_capability(const morph_capability *capability)
{
    const morph_authoring_modules_api *api;
    if (!capability ||
        capability->abi_version < MORPHEUS_AUTHORING_MODULES_ABI_VERSION ||
        capability->api_size < sizeof(morph_authoring_modules_api) ||
        !capability->api) return NULL;
    api = (const morph_authoring_modules_api *)capability->api;
    if (api->abi_version < MORPHEUS_AUTHORING_MODULES_ABI_VERSION ||
        api->struct_size < sizeof(morph_authoring_modules_api) ||
        !api->init || !api->compile_candidate || !api->activate_candidate ||
        !api->activate_candidate_with_state || !api->capture_state ||
        !api->has_candidate || !api->is_active || !api->active_name ||
        !api->render_mode || !api->last_stage || !api->stage_name ||
        !api->reload || !api->update || !api->render_ui ||
        !api->destroy) return NULL;
    return api;
}

typedef struct morph_authoring_agent_api {
    unsigned int abi_version;
    unsigned long struct_size;
    void (*reset)(void *context);
    int (*init)(
        void *context,
        const char *root,
        const char *provider_path,
        char *error,
        unsigned long error_capacity);
    int (*set_model)(
        void *context,
        const char *model,
        char *error,
        unsigned long error_capacity);
    int (*begin)(
        void *context,
        const char *request,
        const char *source_path,
        const char *api_header_path,
        const char *sdk_header_path,
        char *error,
        unsigned long error_capacity);
    int (*start_attempt)(
        void *context,
        const char *diagnostics,
        char *error,
        unsigned long error_capacity);
    int (*poll)(
        void *context,
        int *finished,
        char *error,
        unsigned long error_capacity);
    int (*record_build)(
        void *context,
        int succeeded,
        const char *stage,
        const char *diagnostics,
        char *error,
        unsigned long error_capacity);
    int (*create_patch)(
        void *context,
        char *error,
        unsigned long error_capacity);
    int (*candidate_changed)(
        void *context,
        char *error,
        unsigned long error_capacity);
    int (*accept_source)(
        void *context,
        const char *destination_path,
        char *error,
        unsigned long error_capacity);
    int (*restore_source)(
        void *context,
        const char *destination_path,
        char *error,
        unsigned long error_capacity);
    int (*record_outcome)(
        void *context,
        const char *outcome,
        unsigned long revision,
        char *error,
        unsigned long error_capacity);
    int (*read_provider_log)(
        void *context,
        char *output,
        unsigned long output_capacity);
    void (*cancel)(void *context);
    morph_authoring_agent_status (*status)(void *context);
    unsigned int (*attempt)(void *context);
    const char *(*candidate_path)(void *context);
    const char *(*source_before_path)(void *context);
} morph_authoring_agent_api;

static inline const morph_authoring_agent_api *
morph_authoring_agent_from_capability(const morph_capability *capability)
{
    const morph_authoring_agent_api *api;
    if (!capability ||
        capability->abi_version < MORPHEUS_AUTHORING_AGENT_ABI_VERSION ||
        capability->api_size < sizeof(morph_authoring_agent_api) ||
        !capability->api) return NULL;
    api = (const morph_authoring_agent_api *)capability->api;
    if (api->abi_version < MORPHEUS_AUTHORING_AGENT_ABI_VERSION ||
        api->struct_size < sizeof(morph_authoring_agent_api) ||
        !api->reset || !api->init || !api->set_model || !api->begin ||
        !api->start_attempt || !api->poll || !api->record_build ||
        !api->create_patch || !api->candidate_changed ||
        !api->accept_source || !api->restore_source ||
        !api->record_outcome || !api->read_provider_log || !api->cancel ||
        !api->status || !api->attempt || !api->candidate_path ||
        !api->source_before_path) return NULL;
    return api;
}

typedef struct morph_authoring_export_api {
    unsigned int abi_version;
    unsigned long struct_size;
    void (*reset)(void *context);
    int (*start)(
        void *context,
        const char *source_path,
        const char *output_path,
        const char *application_name,
        const char *bundle_identifier,
        const char *application_version,
        char *error,
        unsigned long error_capacity);
    int (*poll)(
        void *context,
        int *finished,
        char *error,
        unsigned long error_capacity);
    void (*cancel)(void *context);
    morph_authoring_export_status (*status)(void *context);
    const char *(*output_path)(void *context);
    int (*read_log)(
        void *context,
        char *output,
        unsigned long output_capacity);
} morph_authoring_export_api;

static inline const morph_authoring_export_api *
morph_authoring_export_from_capability(const morph_capability *capability)
{
    const morph_authoring_export_api *api;
    if (!capability ||
        capability->abi_version < MORPHEUS_AUTHORING_EXPORT_ABI_VERSION ||
        capability->api_size < sizeof(morph_authoring_export_api) ||
        !capability->api) return NULL;
    api = (const morph_authoring_export_api *)capability->api;
    if (api->abi_version < MORPHEUS_AUTHORING_EXPORT_ABI_VERSION ||
        api->struct_size < sizeof(morph_authoring_export_api) ||
        !api->reset || !api->start || !api->poll || !api->cancel ||
        !api->status || !api->output_path || !api->read_log) return NULL;
    return api;
}

#endif
