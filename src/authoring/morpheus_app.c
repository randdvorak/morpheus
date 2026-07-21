#include "morpheus/authoring_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#include "nuklear.h"

#include "morpheus/authoring.h"

typedef struct morph_authoring_app_state {
    const morph_authoring_controller_api *controller;
    void *controller_context;
    const morph_authoring_projects_api *projects;
    const morph_authoring_revisions_api *revisions;
    const morph_authoring_modules_api *modules;
    const morph_authoring_agent_api *agent;
    const morph_authoring_export_api *export_service;
    char new_project_name[MORPHEUS_AUTHORING_PROJECT_NAME_CAPACITY];
    int new_project_name_length;
    char agent_request[MORPHEUS_AUTHORING_AGENT_REQUEST_CAPACITY];
    int agent_request_length;
    char ollama_model[MORPHEUS_AUTHORING_AGENT_MODEL_CAPACITY];
    int ollama_model_length;
    char export_path[MORPHEUS_AUTHORING_EXPORT_PATH_CAPACITY];
    int export_path_length;
    char dispatch_error[MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY];
} morph_authoring_app_state;

static const morph_capability *find(
    morph_host *host,
    const char *identifier,
    unsigned int abi_version)
{
    return morph_host_find_capability(host, identifier, abi_version);
}

static int authoring_create(morph_host *host, void **state)
{
    morph_authoring_app_state *app;
    const morph_capability *projects;
    const morph_capability *controller;
    const morph_capability *revisions;
    const morph_capability *modules;
    const morph_capability *agent;
    const morph_capability *export_service;
    if (!host || host->abi_version != MORPHEUS_HOST_ABI_VERSION || !state) return 0;
    controller = find(host, MORPHEUS_AUTHORING_CONTROLLER_CAPABILITY,
        MORPHEUS_AUTHORING_CONTROLLER_ABI_VERSION);
    projects = find(host, MORPHEUS_AUTHORING_PROJECTS_CAPABILITY,
        MORPHEUS_AUTHORING_PROJECTS_ABI_VERSION);
    revisions = find(host, MORPHEUS_AUTHORING_REVISIONS_CAPABILITY,
        MORPHEUS_AUTHORING_REVISIONS_ABI_VERSION);
    modules = find(host, MORPHEUS_AUTHORING_MODULES_CAPABILITY,
        MORPHEUS_AUTHORING_MODULES_ABI_VERSION);
    agent = find(host, MORPHEUS_AUTHORING_AGENT_CAPABILITY,
        MORPHEUS_AUTHORING_AGENT_ABI_VERSION);
    export_service = find(host, MORPHEUS_AUTHORING_EXPORT_CAPABILITY,
        MORPHEUS_AUTHORING_EXPORT_ABI_VERSION);
    app = calloc(1, sizeof(*app));
    if (!app) return 0;
    app->controller = morph_authoring_controller_from_capability(controller);
    app->controller_context = controller ? controller->context : NULL;
    app->projects = morph_authoring_projects_from_capability(projects);
    app->revisions = morph_authoring_revisions_from_capability(revisions);
    app->modules = morph_authoring_modules_from_capability(modules);
    app->agent = morph_authoring_agent_from_capability(agent);
    app->export_service = morph_authoring_export_from_capability(export_service);
    if (!app->controller || !app->revisions || !app->modules || !app->agent ||
        !app->export_service) {
        free(app);
        return 0;
    }
    snprintf(app->export_path, sizeof(app->export_path), "MorpheusExport.app");
    app->export_path_length = (int)strlen(app->export_path);
    *state = app;
    return 1;
}

static void authoring_destroy(morph_host *host, void *state)
{
    (void)host;
    free(state);
}

static void authoring_update(morph_host *host, void *state, double dt)
{
    (void)host;
    (void)state;
    (void)dt;
}

static int dispatch(
    morph_authoring_app_state *app,
    morph_authoring_command command,
    unsigned int project_index,
    const char *text)
{
    morph_authoring_request request = {.struct_size = sizeof(request)};
    request.command = command;
    request.project_index = project_index;
    snprintf(request.text, sizeof(request.text), "%s", text ? text : "");
    snprintf(request.model, sizeof(request.model), "%s", app->ollama_model);
    app->dispatch_error[0] = '\0';
    return app->controller->dispatch(
        app->controller_context,
        &request,
        app->dispatch_error,
        sizeof(app->dispatch_error));
}

static void render_fallback(
    morph_host *host,
    morph_authoring_app_state *app,
    const morph_authoring_snapshot *snapshot)
{
    char revision[128];
    host->ui_label(host, "Morpheus authoring UI module: active");
    host->ui_label(host, snapshot->runtime_active
        ? "Runtime C module: active" : "Runtime C module: unavailable");
    if (snapshot->active_name[0]) host->ui_label(host, snapshot->active_name);
    snprintf(revision, sizeof(revision), "Accepted revision: %lu",
        snapshot->active_revision);
    host->ui_label(host, revision);
    if (snapshot->message[0]) host->ui_label(host, snapshot->message);
    if (host->ui_button && snapshot->can_recompile &&
        host->ui_button(host, "Recompile current app")) {
        (void)dispatch(app, MORPHEUS_AUTHORING_COMMAND_RECOMPILE, 0, NULL);
    }
    if (host->ui_button && snapshot->can_rollback &&
        host->ui_button(host, "Rollback previous revision")) {
        (void)dispatch(app, MORPHEUS_AUTHORING_COMMAND_ROLLBACK, 0, NULL);
    }
}

static void render_projects(
    struct nk_context *ctx,
    morph_authoring_app_state *app,
    const morph_authoring_snapshot *snapshot)
{
    const char *names[MORPHEUS_AUTHORING_MAX_PROJECTS];
    unsigned int index;
    int selected;
    if (!snapshot->projects_enabled || !snapshot->project_count) return;
    for (index = 0; index < snapshot->project_count; ++index) {
        names[index] = snapshot->projects[index].name;
    }
    nk_layout_row_dynamic(ctx, 28.0f, 1);
    selected = nk_combo(ctx, names, (int)snapshot->project_count,
        (int)snapshot->active_project_index, 28, nk_vec2(300.0f, 220.0f));
    if (selected != (int)snapshot->active_project_index &&
        snapshot->can_change_project) {
        (void)dispatch(app, MORPHEUS_AUTHORING_COMMAND_SELECT_PROJECT,
            (unsigned int)selected, NULL);
    }
    nk_layout_row_dynamic(ctx, 30.0f, 2);
    nk_edit_string(ctx, NK_EDIT_FIELD, app->new_project_name,
        &app->new_project_name_length,
        (int)sizeof(app->new_project_name) - 1, nk_filter_default);
    app->new_project_name[app->new_project_name_length] = '\0';
    if (nk_button_label(ctx, "New App") && app->new_project_name_length > 0 &&
        snapshot->can_change_project &&
        dispatch(app, MORPHEUS_AUTHORING_COMMAND_CREATE_PROJECT, 0,
            app->new_project_name)) {
        app->new_project_name_length = 0;
        app->new_project_name[0] = '\0';
    }
}

static void render_agent(
    struct nk_context *ctx,
    morph_authoring_app_state *app,
    const morph_authoring_snapshot *snapshot)
{
    nk_layout_row_dynamic(ctx, 24.0f, 1);
    nk_label(ctx, "Agent policy: isolated manual preview", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, 30.0f, 1);
    if (snapshot->can_toggle_agent_provider) {
        if (nk_button_label(ctx, snapshot->agent_uses_ollama
                ? "Provider: Ollama (switch to Codex)"
                : "Provider: Codex (switch to Ollama)")) {
            (void)dispatch(app,
                MORPHEUS_AUTHORING_COMMAND_TOGGLE_AGENT_PROVIDER, 0, NULL);
        }
    } else {
        nk_label(ctx, snapshot->agent_provider_is_custom
            ? "Provider: custom executable"
            : (snapshot->agent_uses_ollama
                ? "Provider: Ollama" : "Provider: Codex"), NK_TEXT_LEFT);
    }
    if (snapshot->agent_uses_ollama && !snapshot->agent_provider_is_custom) {
        nk_layout_row_dynamic(ctx, 24.0f, 1);
        nk_label(ctx, "Ollama model (blank selects first installed):", NK_TEXT_LEFT);
        nk_layout_row_dynamic(ctx, 30.0f, 1);
        nk_edit_string(ctx, NK_EDIT_FIELD, app->ollama_model,
            &app->ollama_model_length,
            (int)sizeof(app->ollama_model) - 1, nk_filter_default);
        app->ollama_model[app->ollama_model_length] = '\0';
    }
    nk_layout_row_dynamic(ctx, 24.0f, 1);
    nk_label(ctx, "Describe a generated application change:", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, 72.0f, 1);
    nk_edit_string(ctx, NK_EDIT_BOX, app->agent_request,
        &app->agent_request_length,
        (int)sizeof(app->agent_request) - 1, nk_filter_default);
    app->agent_request[app->agent_request_length] = '\0';
    nk_layout_row_dynamic(ctx, 34.0f, 1);
    if (snapshot->can_submit_agent && app->agent_request_length > 0 &&
        nk_button_label(ctx, "Ask coding agent")) {
        (void)dispatch(app, MORPHEUS_AUTHORING_COMMAND_SUBMIT_AGENT, 0,
            app->agent_request);
    }
    nk_layout_row_dynamic(ctx, 24.0f, 1);
    nk_label(ctx, snapshot->agent_status, NK_TEXT_LEFT);
    if (snapshot->agent_preview_active) {
        nk_layout_row_dynamic(ctx, 34.0f, 2);
        if (snapshot->can_accept_preview &&
            nk_button_label(ctx, "Accept preview") &&
            dispatch(app, MORPHEUS_AUTHORING_COMMAND_ACCEPT_PREVIEW, 0, NULL)) {
            app->agent_request_length = 0;
            app->agent_request[0] = '\0';
        }
        if (snapshot->can_reject_preview &&
            nk_button_label(ctx, "Reject preview")) {
            (void)dispatch(app, MORPHEUS_AUTHORING_COMMAND_REJECT_PREVIEW, 0, NULL);
        }
    }
}

static void render_export(
    struct nk_context *ctx,
    morph_authoring_app_state *app,
    const morph_authoring_snapshot *snapshot)
{
    nk_layout_row_dynamic(ctx, 24.0f, 1);
    nk_label(ctx, "Standalone export path:", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, 30.0f, 1);
    nk_edit_string(ctx, NK_EDIT_FIELD, app->export_path,
        &app->export_path_length,
        (int)sizeof(app->export_path) - 1, nk_filter_default);
    app->export_path[app->export_path_length] = '\0';
    nk_layout_row_dynamic(ctx, 32.0f, 1);
    if (snapshot->can_cancel_export) {
        if (nk_button_label(ctx, "Cancel export")) {
            (void)dispatch(app, MORPHEUS_AUTHORING_COMMAND_CANCEL_EXPORT, 0, NULL);
        }
    } else if (snapshot->can_start_export && app->export_path_length > 0 &&
        nk_button_label(ctx, "Export standalone app")) {
        (void)dispatch(app, MORPHEUS_AUTHORING_COMMAND_START_EXPORT, 0,
            app->export_path);
    }
    if (snapshot->export_status_text[0]) {
        nk_layout_row_dynamic(ctx, 48.0f, 1);
        nk_label_wrap(ctx, snapshot->export_status_text);
    }
}

static void authoring_render(morph_host *host, void *state)
{
    morph_authoring_app_state *app = state;
    morph_authoring_snapshot snapshot = {.struct_size = sizeof(snapshot)};
    struct nk_context *ctx;
    char revision[128];
    if (!host || !app || !host->ui_label) return;
    if (!app->controller->snapshot(app->controller_context, &snapshot)) {
        host->ui_label(host, "Authoring controller unavailable");
        return;
    }
    ctx = host->nuklear;
    if (!ctx) {
        render_fallback(host, app, &snapshot);
        return;
    }
    nk_layout_row_dynamic(ctx, 24.0f, 1);
    nk_label(ctx, "Morpheus authoring UI module: active", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, 24.0f, 1);
    nk_label(ctx, snapshot.runtime_active
        ? "Runtime C module: active" : "Runtime C module: unavailable", NK_TEXT_LEFT);
    if (snapshot.active_name[0]) {
        nk_layout_row_dynamic(ctx, 24.0f, 1);
        nk_label(ctx, snapshot.active_name, NK_TEXT_LEFT);
    }
    snprintf(revision, sizeof(revision), "Accepted revision: %lu",
        snapshot.active_revision);
    nk_layout_row_dynamic(ctx, 24.0f, 1);
    nk_label(ctx, revision, NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, 30.0f, 2);
    if (snapshot.can_recompile && nk_button_label(ctx, "Recompile")) {
        (void)dispatch(app, MORPHEUS_AUTHORING_COMMAND_RECOMPILE, 0, NULL);
    }
    if (snapshot.can_rollback && nk_button_label(ctx, "Rollback")) {
        (void)dispatch(app, MORPHEUS_AUTHORING_COMMAND_ROLLBACK, 0, NULL);
    }
    render_projects(ctx, app, &snapshot);
    if (snapshot.recovered_from_crash) {
        nk_layout_row_dynamic(ctx, 24.0f, 1);
        nk_label(ctx, "Safe mode: previous revision exited abnormally", NK_TEXT_LEFT);
    }
    render_agent(ctx, app, &snapshot);
    render_export(ctx, app, &snapshot);
    if (snapshot.diagnostics[0] || app->dispatch_error[0]) {
        nk_layout_row_dynamic(ctx, 72.0f, 1);
        nk_label_wrap(ctx, app->dispatch_error[0]
            ? app->dispatch_error : snapshot.diagnostics);
    }
}

static int authoring_save(
    morph_host *host,
    void *state,
    const void **data,
    unsigned long *size)
{
    (void)host;
    (void)state;
    if (!data || !size) return 0;
    *data = NULL;
    *size = 0;
    return 1;
}

static int authoring_load(
    morph_host *host,
    void **state,
    const void *data,
    unsigned long size)
{
    if (!state || size != 0 || data) return 0;
    return *state ? 1 : authoring_create(host, state);
}

static const morph_app_api authoring_api = {
    MORPHEUS_APP_ABI_VERSION,
    "Morpheus Authoring UI",
    authoring_create,
    authoring_destroy,
    authoring_update,
    authoring_render,
    authoring_save,
    authoring_load
};

const morph_app_api *morph_app_entry(void)
{
    return &authoring_api;
}

const morph_app_api *morph_authoring_app_entry(void)
{
    return morph_app_entry();
}
