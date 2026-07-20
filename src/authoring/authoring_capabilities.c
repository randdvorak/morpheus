#include "authoring_capabilities.h"

#include <stdio.h>

#include "agent_session.h"
#include "export_service.h"
#include "project_store.h"
#include "revision_store.h"
#include "runtime_module.h"

_Static_assert(MORPHEUS_AUTHORING_MAX_PROJECTS == MORPH_PROJECT_MAX_PROJECTS,
    "Public and internal project limits must agree");
_Static_assert(MORPHEUS_AUTHORING_REVISION_PATH_CAPACITY == MORPH_REVISION_PATH_CAPACITY,
    "Public and internal revision path limits must agree");
_Static_assert((int)MORPHEUS_AUTHORING_MODULE_IDLE == (int)MORPH_RUNTIME_STAGE_IDLE &&
    (int)MORPHEUS_AUTHORING_MODULE_COMPILE == (int)MORPH_RUNTIME_STAGE_COMPILE &&
    (int)MORPHEUS_AUTHORING_MODULE_VALIDATE == (int)MORPH_RUNTIME_STAGE_VALIDATE &&
    (int)MORPHEUS_AUTHORING_MODULE_SAVE_STATE == (int)MORPH_RUNTIME_STAGE_SAVE_STATE &&
    (int)MORPHEUS_AUTHORING_MODULE_INITIALIZE == (int)MORPH_RUNTIME_STAGE_INITIALIZE &&
    (int)MORPHEUS_AUTHORING_MODULE_MIGRATE == (int)MORPH_RUNTIME_STAGE_MIGRATE &&
    (int)MORPHEUS_AUTHORING_MODULE_ACTIVE == (int)MORPH_RUNTIME_STAGE_ACTIVE,
    "Public and internal module stages must agree");
_Static_assert(MORPHEUS_AUTHORING_AGENT_PATH_CAPACITY == MORPH_AGENT_PATH_CAPACITY &&
    MORPHEUS_AUTHORING_AGENT_REQUEST_CAPACITY == MORPH_AGENT_REQUEST_CAPACITY &&
    MORPHEUS_AUTHORING_AGENT_MODEL_CAPACITY == MORPH_AGENT_MODEL_CAPACITY &&
    MORPHEUS_AUTHORING_AGENT_MAX_ATTEMPTS == MORPH_AGENT_MAX_ATTEMPTS,
    "Public and internal agent limits must agree");
_Static_assert((int)MORPHEUS_AUTHORING_AGENT_IDLE == (int)MORPH_AGENT_IDLE &&
    (int)MORPHEUS_AUTHORING_AGENT_RUNNING == (int)MORPH_AGENT_RUNNING &&
    (int)MORPHEUS_AUTHORING_AGENT_PROVIDER_SUCCEEDED ==
        (int)MORPH_AGENT_PROVIDER_SUCCEEDED &&
    (int)MORPHEUS_AUTHORING_AGENT_PROVIDER_FAILED ==
        (int)MORPH_AGENT_PROVIDER_FAILED,
    "Public and internal agent states must agree");

static int projects_init(
    void *context,
    const char *projects_root,
    char *error,
    unsigned long error_capacity)
{
    return context && projects_root && morph_project_store_init(
        context, projects_root, error, error_capacity);
}

static unsigned int projects_count(void *context)
{
    const morph_project_store *store = context;
    return store ? store->count : 0;
}

static unsigned int projects_active_index(void *context)
{
    const morph_project_store *store = context;
    return store && store->active_index < store->count ? store->active_index : 0;
}

static int projects_project(
    void *context,
    unsigned int index,
    morph_authoring_project_info *project)
{
    const morph_project_store *store = context;
    if (!store || !project || index >= store->count) return 0;
    snprintf(project->name, sizeof(project->name), "%s", store->projects[index].name);
    snprintf(project->slug, sizeof(project->slug), "%s", store->projects[index].slug);
    return 1;
}

static int projects_create(
    void *context,
    const char *name,
    char *error,
    unsigned long error_capacity)
{
    return context && morph_project_store_create(
        context, name, error, error_capacity);
}

static int projects_select(
    void *context,
    unsigned int index,
    char *error,
    unsigned long error_capacity)
{
    return context && morph_project_store_select(
        context, index, error, error_capacity);
}

static int projects_paths(
    void *context,
    char *workspace,
    unsigned long workspace_capacity,
    char *source,
    unsigned long source_capacity,
    char *assets,
    unsigned long assets_capacity)
{
    return context && morph_project_store_paths(
        context,
        workspace,
        workspace_capacity,
        source,
        source_capacity,
        assets,
        assets_capacity);
}

static const morph_authoring_projects_api projects_api = {
    MORPHEUS_AUTHORING_PROJECTS_ABI_VERSION,
    sizeof(morph_authoring_projects_api),
    projects_init,
    projects_count,
    projects_active_index,
    projects_project,
    projects_create,
    projects_select,
    projects_paths
};

morph_capability morph_authoring_projects_capability(
    morph_project_store *store)
{
    const morph_capability capability = {
        MORPHEUS_AUTHORING_PROJECTS_CAPABILITY,
        MORPHEUS_AUTHORING_PROJECTS_ABI_VERSION,
        sizeof(projects_api),
        &projects_api,
        store
    };
    return capability;
}

static int revisions_init(
    void *context,
    const char *workspace_root,
    char *error,
    unsigned long error_capacity)
{
    return context && workspace_root && morph_revision_store_init(
        context, workspace_root, error, error_capacity);
}

static unsigned long revisions_active(void *context)
{
    const morph_revision_store *store = context;
    return store ? store->active_revision : 0;
}

static unsigned long revisions_latest(void *context)
{
    const morph_revision_store *store = context;
    return store ? store->latest_revision : 0;
}

static int revisions_checkpoint(
    void *context,
    const char *source_path,
    const void *state_data,
    unsigned long state_size,
    unsigned long *revision,
    char *error,
    unsigned long error_capacity)
{
    return context && morph_revision_store_checkpoint(
        context,
        source_path,
        state_data,
        state_size,
        revision,
        error,
        error_capacity);
}

static int revisions_load(
    void *context,
    unsigned long revision,
    char *source_path,
    unsigned long source_path_capacity,
    void **state_data,
    unsigned long *state_size,
    char *error,
    unsigned long error_capacity)
{
    return context && morph_revision_store_load(
        context,
        revision,
        source_path,
        source_path_capacity,
        state_data,
        state_size,
        error,
        error_capacity);
}

static void revisions_release_state(void *context, void *state_data)
{
    (void)context;
    morph_revision_store_release_state(state_data);
}

static int revisions_previous(void *context, unsigned long *revision)
{
    return context && morph_revision_store_previous(context, revision);
}

static int revisions_set_active(
    void *context,
    unsigned long revision,
    char *error,
    unsigned long error_capacity)
{
    return context && morph_revision_store_set_active(
        context, revision, error, error_capacity);
}

static int revisions_begin_session(
    void *context,
    int *recovered_from_crash,
    char *error,
    unsigned long error_capacity)
{
    return context && morph_revision_store_begin_session(
        context, recovered_from_crash, error, error_capacity);
}

static int revisions_refresh_session(
    void *context,
    char *error,
    unsigned long error_capacity)
{
    return context && morph_revision_store_refresh_session(
        context, error, error_capacity);
}

static int revisions_end_session(
    void *context,
    char *error,
    unsigned long error_capacity)
{
    return context && morph_revision_store_end_session(
        context, error, error_capacity);
}

static int revisions_record_attempt(
    void *context,
    const char *stage,
    int succeeded,
    const char *message,
    char *error,
    unsigned long error_capacity)
{
    return context && morph_revision_store_record_attempt(
        context, stage, succeeded, message, error, error_capacity);
}

static const morph_authoring_revisions_api revisions_api = {
    MORPHEUS_AUTHORING_REVISIONS_ABI_VERSION,
    sizeof(morph_authoring_revisions_api),
    revisions_init,
    revisions_active,
    revisions_latest,
    revisions_checkpoint,
    revisions_load,
    revisions_release_state,
    revisions_previous,
    revisions_set_active,
    revisions_begin_session,
    revisions_refresh_session,
    revisions_end_session,
    revisions_record_attempt
};

morph_capability morph_authoring_revisions_capability(
    morph_revision_store *store)
{
    const morph_capability capability = {
        MORPHEUS_AUTHORING_REVISIONS_CAPABILITY,
        MORPHEUS_AUTHORING_REVISIONS_ABI_VERSION,
        sizeof(revisions_api),
        &revisions_api,
        store
    };
    return capability;
}

static void modules_init(void *context)
{
    if (context) morph_runtime_module_init(context);
}

static int modules_compile_candidate(
    void *context,
    const char *source_path,
    char *error,
    unsigned long error_capacity)
{
    return context && source_path && morph_runtime_module_compile_candidate(
        context, source_path, error, error_capacity);
}

static int modules_activate_candidate(
    void *context,
    morph_host *host,
    char *error,
    unsigned long error_capacity)
{
    return context && host && morph_runtime_module_activate_candidate(
        context, host, error, error_capacity);
}

static int modules_activate_candidate_with_state(
    void *context,
    morph_host *host,
    const void *state_data,
    unsigned long state_size,
    char *error,
    unsigned long error_capacity)
{
    return context && host && morph_runtime_module_activate_candidate_with_state(
        context, host, state_data, state_size, error, error_capacity);
}

static int modules_capture_state(
    void *context,
    morph_host *host,
    const void **state_data,
    unsigned long *state_size,
    char *error,
    unsigned long error_capacity)
{
    return context && host && morph_runtime_module_capture_state(
        context, host, state_data, state_size, error, error_capacity);
}

static int modules_has_candidate(void *context)
{
    return context && morph_runtime_module_has_candidate(context);
}

static int modules_is_active(void *context)
{
    const morph_runtime_module *module = context;
    return module && module->api;
}

static const char *modules_active_name(void *context)
{
    const morph_runtime_module *module = context;
    return module && module->api ? module->api->name : NULL;
}

static unsigned int modules_render_mode(void *context)
{
    return context
        ? morph_runtime_module_render_mode(context)
        : MORPHEUS_RENDER_EMBEDDED;
}

static morph_authoring_module_stage modules_last_stage(void *context)
{
    const morph_runtime_module *module = context;
    return module
        ? (morph_authoring_module_stage)module->last_stage
        : MORPHEUS_AUTHORING_MODULE_IDLE;
}

static const char *modules_stage_name(
    void *context,
    morph_authoring_module_stage stage)
{
    (void)context;
    return morph_runtime_stage_name((morph_runtime_stage)stage);
}

static int modules_reload(
    void *context,
    morph_host *host,
    const char *source_path,
    char *error,
    unsigned long error_capacity)
{
    return context && host && source_path && morph_runtime_module_reload(
        context, host, source_path, error, error_capacity);
}

static void modules_update(void *context, morph_host *host, double dt)
{
    if (context && host) morph_runtime_module_update(context, host, dt);
}

static void modules_render_ui(void *context, morph_host *host)
{
    if (context && host) morph_runtime_module_render_ui(context, host);
}

static void modules_destroy(void *context, morph_host *host)
{
    if (context && host) morph_runtime_module_destroy(context, host);
}

static const morph_authoring_modules_api modules_api = {
    MORPHEUS_AUTHORING_MODULES_ABI_VERSION,
    sizeof(morph_authoring_modules_api),
    modules_init,
    modules_compile_candidate,
    modules_activate_candidate,
    modules_activate_candidate_with_state,
    modules_capture_state,
    modules_has_candidate,
    modules_is_active,
    modules_active_name,
    modules_render_mode,
    modules_last_stage,
    modules_stage_name,
    modules_reload,
    modules_update,
    modules_render_ui,
    modules_destroy
};

morph_capability morph_authoring_modules_capability(
    morph_runtime_module *module)
{
    const morph_capability capability = {
        MORPHEUS_AUTHORING_MODULES_CAPABILITY,
        MORPHEUS_AUTHORING_MODULES_ABI_VERSION,
        sizeof(modules_api),
        &modules_api,
        module
    };
    return capability;
}

static void agent_reset(void *context)
{
    if (context) morph_agent_session_reset(context);
}

static int agent_init(
    void *context,
    const char *root,
    const char *provider_path,
    char *error,
    unsigned long error_capacity)
{
    return context && morph_agent_session_init(
        context, root, provider_path, error, error_capacity);
}

static int agent_set_model(
    void *context,
    const char *model,
    char *error,
    unsigned long error_capacity)
{
    return context && morph_agent_session_set_model(
        context, model, error, error_capacity);
}

static int agent_begin(
    void *context,
    const char *request,
    const char *source_path,
    const char *api_header_path,
    const char *sdk_header_path,
    char *error,
    unsigned long error_capacity)
{
    return context && morph_agent_session_begin(
        context,
        request,
        source_path,
        api_header_path,
        sdk_header_path,
        error,
        error_capacity);
}

static int agent_start_attempt(
    void *context,
    const char *diagnostics,
    char *error,
    unsigned long error_capacity)
{
    return context && morph_agent_session_start_attempt(
        context, diagnostics, error, error_capacity);
}

static int agent_poll(
    void *context,
    int *finished,
    char *error,
    unsigned long error_capacity)
{
    return context && morph_agent_session_poll(
        context, finished, error, error_capacity);
}

static int agent_record_build(
    void *context,
    int succeeded,
    const char *stage,
    const char *diagnostics,
    char *error,
    unsigned long error_capacity)
{
    return context && morph_agent_session_record_build(
        context, succeeded, stage, diagnostics, error, error_capacity);
}

static int agent_create_patch(
    void *context,
    char *error,
    unsigned long error_capacity)
{
    return context && morph_agent_session_create_patch(
        context, error, error_capacity);
}

static int agent_candidate_changed(
    void *context,
    char *error,
    unsigned long error_capacity)
{
    return context && morph_agent_session_candidate_changed(
        context, error, error_capacity);
}

static int agent_accept_source(
    void *context,
    const char *destination_path,
    char *error,
    unsigned long error_capacity)
{
    return context && morph_agent_session_accept_source(
        context, destination_path, error, error_capacity);
}

static int agent_restore_source(
    void *context,
    const char *destination_path,
    char *error,
    unsigned long error_capacity)
{
    return context && morph_agent_session_restore_source(
        context, destination_path, error, error_capacity);
}

static int agent_record_outcome(
    void *context,
    const char *outcome,
    unsigned long revision,
    char *error,
    unsigned long error_capacity)
{
    return context && morph_agent_session_record_outcome(
        context, outcome, revision, error, error_capacity);
}

static int agent_read_provider_log(
    void *context,
    char *output,
    unsigned long output_capacity)
{
    return context && morph_agent_session_read_provider_log(
        context, output, output_capacity);
}

static void agent_cancel(void *context)
{
    if (context) morph_agent_session_cancel(context);
}

static morph_authoring_agent_status agent_status(void *context)
{
    const morph_agent_session *session = context;
    return session
        ? (morph_authoring_agent_status)session->status
        : MORPHEUS_AUTHORING_AGENT_IDLE;
}

static unsigned int agent_attempt(void *context)
{
    const morph_agent_session *session = context;
    return session ? session->attempt : 0;
}

static const char *agent_candidate_path(void *context)
{
    const morph_agent_session *session = context;
    return session ? session->candidate_path : NULL;
}

static const char *agent_source_before_path(void *context)
{
    const morph_agent_session *session = context;
    return session ? session->source_before_path : NULL;
}

static const morph_authoring_agent_api agent_api = {
    MORPHEUS_AUTHORING_AGENT_ABI_VERSION,
    sizeof(morph_authoring_agent_api),
    agent_reset,
    agent_init,
    agent_set_model,
    agent_begin,
    agent_start_attempt,
    agent_poll,
    agent_record_build,
    agent_create_patch,
    agent_candidate_changed,
    agent_accept_source,
    agent_restore_source,
    agent_record_outcome,
    agent_read_provider_log,
    agent_cancel,
    agent_status,
    agent_attempt,
    agent_candidate_path,
    agent_source_before_path
};

morph_capability morph_authoring_agent_capability(
    morph_agent_session *session)
{
    const morph_capability capability = {
        MORPHEUS_AUTHORING_AGENT_CAPABILITY,
        MORPHEUS_AUTHORING_AGENT_ABI_VERSION,
        sizeof(agent_api),
        &agent_api,
        session
    };
    return capability;
}

static void export_reset(void *context)
{
    if (context) morph_export_service_reset(context);
}

static int export_start(
    void *context,
    const char *source_path,
    const char *output_path,
    const char *application_name,
    const char *bundle_identifier,
    const char *application_version,
    char *error,
    unsigned long error_capacity)
{
    return context && morph_export_service_start(
        context,
        source_path,
        output_path,
        application_name,
        bundle_identifier,
        application_version,
        error,
        error_capacity);
}

static int export_poll(
    void *context,
    int *finished,
    char *error,
    unsigned long error_capacity)
{
    return context && morph_export_service_poll(
        context, finished, error, error_capacity);
}

static void export_cancel(void *context)
{
    if (context) morph_export_service_cancel(context);
}

static morph_authoring_export_status export_status(void *context)
{
    const morph_export_service *service = context;
    return service ? service->status : MORPHEUS_AUTHORING_EXPORT_IDLE;
}

static const char *export_output_path(void *context)
{
    const morph_export_service *service = context;
    return service ? service->output_path : NULL;
}

static int export_read_log(
    void *context,
    char *output,
    unsigned long output_capacity)
{
    return context && morph_export_service_read_log(
        context, output, output_capacity);
}

static const morph_authoring_export_api export_api = {
    MORPHEUS_AUTHORING_EXPORT_ABI_VERSION,
    sizeof(morph_authoring_export_api),
    export_reset,
    export_start,
    export_poll,
    export_cancel,
    export_status,
    export_output_path,
    export_read_log
};

morph_capability morph_authoring_export_capability(
    morph_export_service *service,
    const char *tool_path)
{
    const morph_capability capability = {
        MORPHEUS_AUTHORING_EXPORT_CAPABILITY,
        MORPHEUS_AUTHORING_EXPORT_ABI_VERSION,
        sizeof(export_api),
        &export_api,
        service
    };
    morph_export_service_init(
        service,
        tool_path ? tool_path : MORPHEUS_EXPORT_TOOL_PATH);
    return capability;
}
