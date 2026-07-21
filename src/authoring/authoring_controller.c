#include "authoring_controller.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_text(char *destination, unsigned long capacity, const char *text)
{
    if (!destination || !capacity) return;
    snprintf(destination, (size_t)capacity, "%s", text ? text : "");
}

static morph_authoring_agent_status unavailable_agent_status(void *context)
{
    (void)context;
    return MORPHEUS_AUTHORING_AGENT_IDLE;
}

static morph_authoring_export_status unavailable_export_status(void *context)
{
    (void)context;
    return MORPHEUS_AUTHORING_EXPORT_IDLE;
}

static const morph_authoring_agent_api unavailable_agent = {
    .abi_version = MORPHEUS_AUTHORING_AGENT_ABI_VERSION,
    .struct_size = sizeof(morph_authoring_agent_api),
    .status = unavailable_agent_status
};

static const morph_authoring_export_api unavailable_export = {
    .abi_version = MORPHEUS_AUTHORING_EXPORT_ABI_VERSION,
    .struct_size = sizeof(morph_authoring_export_api),
    .status = unavailable_export_status
};

static const morph_capability *find_capability(
    const morph_capability_registry *capabilities,
    const char *identifier,
    unsigned int abi_version)
{
    morph_host host = {0};
    host.abi_version = MORPHEUS_HOST_ABI_VERSION;
    host.capabilities = capabilities;
    return morph_host_find_capability(&host, identifier, abi_version);
}

static int workflow_idle(const morph_authoring_controller *controller)
{
    return controller && controller->started &&
        controller->pending_request.command == MORPHEUS_AUTHORING_COMMAND_NONE &&
        controller->agent->status(controller->agent_context) !=
            MORPHEUS_AUTHORING_AGENT_RUNNING &&
        controller->export_service->status(controller->export_context) !=
            MORPHEUS_AUTHORING_EXPORT_RUNNING;
}

static void record_attempt(
    morph_authoring_controller *controller,
    int succeeded,
    const char *message)
{
    char ignored[MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY];
    if (!controller->revision_store_ready) return;
    (void)controller->revisions->record_attempt(
        controller->revisions_context,
        controller->modules->stage_name(
            controller->modules_context,
            controller->modules->last_stage(controller->modules_context)),
        succeeded,
        message,
        ignored,
        sizeof(ignored));
}

static int checkpoint_active(
    morph_authoring_controller *controller,
    const char *source_path,
    char *error)
{
    const void *state_data = NULL;
    unsigned long state_size = 0;
    return controller->modules->capture_state(
            controller->modules_context,
            controller->runtime_host,
            &state_data,
            &state_size,
            error,
            MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY) &&
        controller->revisions->checkpoint(
            controller->revisions_context,
            source_path,
            state_data,
            state_size,
            NULL,
            error,
            MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY);
}

static int copy_runtime_state(
    morph_authoring_controller *controller,
    char *error)
{
    const void *state_data = NULL;
    unsigned long state_size = 0;
    void *copy = NULL;
    free(controller->preview_restore_state);
    controller->preview_restore_state = NULL;
    controller->preview_restore_state_size = 0;
    if (!controller->modules->capture_state(
            controller->modules_context,
            controller->runtime_host,
            &state_data,
            &state_size,
            error,
            MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY)) return 0;
    if (state_size) {
        copy = malloc((size_t)state_size);
        if (!copy) {
            set_text(error, MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY,
                "Unable to preserve preview rollback state");
            return 0;
        }
        memcpy(copy, state_data, (size_t)state_size);
    }
    controller->preview_restore_state = copy;
    controller->preview_restore_state_size = state_size;
    return 1;
}

static int initialize_agent(morph_authoring_controller *controller, char *error)
{
    controller->agent->cancel(controller->agent_context);
    controller->agent->reset(controller->agent_context);
    snprintf(controller->agent_root, sizeof(controller->agent_root),
        "%s/agent", controller->workspace_root);
    controller->agent_ready = controller->revision_store_ready &&
        controller->agent->init(
            controller->agent_context,
            controller->agent_root,
            controller->agent_provider_path,
            error,
            MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY);
    if (controller->agent_ready) {
        controller->agent_ready = controller->agent->set_model(
            controller->agent_context,
            controller->agent_uses_ollama ? controller->ollama_model : "",
            error,
            MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY);
    }
    set_text(controller->agent_status, sizeof(controller->agent_status),
        controller->agent_ready ? "Agent ready" : "Agent unavailable");
    return controller->agent_ready;
}

static int load_current_project(morph_authoring_controller *controller, char *error)
{
    char accepted_source[MORPHEUS_AUTHORING_REVISION_PATH_CAPACITY];
    void *accepted_state = NULL;
    unsigned long accepted_state_size = 0;
    int loaded = 0;

    controller->revision_store_ready = controller->revisions->init(
        controller->revisions_context,
        controller->workspace_root,
        error,
        MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY);
    if (controller->revision_store_ready) {
        controller->revision_store_ready = controller->revisions->begin_session(
            controller->revisions_context,
            &controller->recovered_from_crash,
            error,
            MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY);
    }
    if (controller->revision_store_ready && controller->recovered_from_crash) {
        (void)controller->revisions->record_attempt(
            controller->revisions_context,
            "startup",
            0,
            "abnormal exit detected; automatic rollback",
            error,
            MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY);
    }
    (void)initialize_agent(controller, error);

    if (controller->revision_store_ready &&
        controller->revisions->active_revision(controller->revisions_context)) {
        loaded = controller->revisions->load(
                controller->revisions_context,
                controller->revisions->active_revision(controller->revisions_context),
                accepted_source,
                sizeof(accepted_source),
                &accepted_state,
                &accepted_state_size,
                error,
                MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY) &&
            controller->modules->compile_candidate(
                controller->modules_context,
                accepted_source,
                error,
                MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY) &&
            controller->modules->activate_candidate_with_state(
                controller->modules_context,
                controller->runtime_host,
                accepted_state,
                accepted_state_size,
                error,
                MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY);
        controller->revisions->release_state(
            controller->revisions_context, accepted_state);
        record_attempt(controller, loaded, error);
        if (!loaded) {
            (void)controller->revisions->set_active(
                controller->revisions_context,
                0,
                error,
                MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY);
        }
    }
    if (!loaded) {
        loaded = controller->modules->reload(
            controller->modules_context,
            controller->runtime_host,
            controller->source_path,
            error,
            MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY);
        record_attempt(controller, loaded, error);
        if (loaded && controller->revision_store_ready &&
            !controller->revisions->active_revision(controller->revisions_context)) {
            loaded = checkpoint_active(controller, controller->source_path, error);
        }
    }
    if (loaded && controller->revision_store_ready) {
        loaded = controller->revisions->refresh_session(
            controller->revisions_context,
            error,
            MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY);
    }
    set_text(controller->diagnostics, sizeof(controller->diagnostics),
        loaded ? "" : error);
    set_text(controller->message, sizeof(controller->message),
        loaded ? "Project ready" : "Project failed to load");
    return loaded;
}

static int recompile(morph_authoring_controller *controller, char *error)
{
    int succeeded = controller->modules->compile_candidate(
            controller->modules_context,
            controller->source_path,
            error,
            MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY) &&
        controller->modules->activate_candidate(
            controller->modules_context,
            controller->runtime_host,
            error,
            MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY);
    record_attempt(controller, succeeded, error);
    if (succeeded && controller->revision_store_ready) {
        succeeded = checkpoint_active(controller, controller->source_path, error) &&
            controller->revisions->refresh_session(
                controller->revisions_context,
                error,
                MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY);
    }
    return succeeded;
}

static int rollback(morph_authoring_controller *controller, char *error)
{
    char source[MORPHEUS_AUTHORING_REVISION_PATH_CAPACITY];
    void *state = NULL;
    unsigned long state_size = 0;
    unsigned long revision = 0;
    int succeeded = controller->revisions->previous(
            controller->revisions_context, &revision) &&
        controller->revisions->load(
            controller->revisions_context,
            revision,
            source,
            sizeof(source),
            &state,
            &state_size,
            error,
            MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY) &&
        controller->modules->compile_candidate(
            controller->modules_context,
            source,
            error,
            MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY) &&
        controller->modules->activate_candidate_with_state(
            controller->modules_context,
            controller->runtime_host,
            state,
            state_size,
            error,
            MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY) &&
        controller->revisions->set_active(
            controller->revisions_context,
            revision,
            error,
            MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY) &&
        controller->revisions->refresh_session(
            controller->revisions_context,
            error,
            MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY);
    controller->revisions->release_state(controller->revisions_context, state);
    record_attempt(controller, succeeded, error);
    return succeeded;
}

static int switch_project(
    morph_authoring_controller *controller,
    unsigned int project_index,
    char *error)
{
    if (!controller->projects_enabled || !controller->projects) {
        set_text(error, MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY,
            "Project switching is unavailable");
        return 0;
    }
    if (controller->modules->is_active(controller->modules_context) &&
        controller->revision_store_ready) {
        (void)checkpoint_active(controller, controller->source_path, error);
        (void)controller->revisions->end_session(
            controller->revisions_context,
            error,
            MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY);
    }
    controller->modules->destroy(
        controller->modules_context, controller->runtime_host);
    controller->modules->init(controller->modules_context);
    controller->revision_store_ready = 0;
    controller->agent_preview_active = 0;
    controller->recovered_from_crash = 0;
    free(controller->preview_restore_state);
    controller->preview_restore_state = NULL;
    controller->preview_restore_state_size = 0;
    if (!controller->projects->select(
            controller->projects_context,
            project_index,
            error,
            MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY) ||
        !controller->projects->paths(
            controller->projects_context,
            controller->workspace_root,
            sizeof(controller->workspace_root),
            controller->source_path,
            sizeof(controller->source_path),
            controller->assets_root,
            sizeof(controller->assets_root))) return 0;
    return load_current_project(controller, error);
}

static int submit_agent(
    morph_authoring_controller *controller,
    const morph_authoring_request *request,
    char *error)
{
    char accepted_source[MORPHEUS_AUTHORING_REVISION_PATH_CAPACITY];
    void *accepted_state = NULL;
    unsigned long accepted_state_size = 0;
    const char *base_source = controller->source_path;
    int ready = controller->agent_ready && request->text[0];
    if (request->model[0] || controller->agent_uses_ollama) {
        set_text(controller->ollama_model, sizeof(controller->ollama_model),
            request->model);
    }
    if (ready && controller->revision_store_ready &&
        controller->revisions->active_revision(controller->revisions_context)) {
        ready = controller->revisions->load(
            controller->revisions_context,
            controller->revisions->active_revision(controller->revisions_context),
            accepted_source,
            sizeof(accepted_source),
            &accepted_state,
            &accepted_state_size,
            error,
            MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY);
        if (ready) base_source = accepted_source;
    }
    if (ready) ready = controller->agent->set_model(
        controller->agent_context,
        controller->agent_uses_ollama ? controller->ollama_model : "",
        error,
        MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY);
    if (ready) ready = controller->agent->begin(
        controller->agent_context,
        request->text,
        base_source,
        controller->api_header_path,
        controller->sdk_header_path,
        error,
        MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY);
    controller->revisions->release_state(
        controller->revisions_context, accepted_state);
    if (ready) ready = controller->agent->start_attempt(
        controller->agent_context,
        "",
        error,
        MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY);
    if (ready) {
        snprintf(controller->agent_status, sizeof(controller->agent_status),
            "Agent attempt 1 of %d running",
            MORPHEUS_AUTHORING_AGENT_MAX_ATTEMPTS);
    }
    return ready;
}

static int accept_preview(morph_authoring_controller *controller, char *error)
{
    int source_updated;
    int accepted;
    if (!controller->agent_preview_active || !controller->revision_store_ready) return 0;
    source_updated = controller->agent->accept_source(
        controller->agent_context,
        controller->source_path,
        error,
        MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY);
    accepted = source_updated && checkpoint_active(
        controller,
        controller->agent->candidate_path(controller->agent_context),
        error);
    if (source_updated && !accepted) {
        (void)controller->agent->restore_source(
            controller->agent_context,
            controller->source_path,
            error,
            MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY);
    }
    if (!accepted) return 0;
    (void)controller->revisions->refresh_session(
        controller->revisions_context,
        error,
        MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY);
    (void)controller->agent->record_outcome(
        controller->agent_context,
        "accepted",
        controller->revisions->active_revision(controller->revisions_context),
        error,
        MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY);
    free(controller->preview_restore_state);
    controller->preview_restore_state = NULL;
    controller->preview_restore_state_size = 0;
    controller->agent_preview_active = 0;
    snprintf(controller->agent_status, sizeof(controller->agent_status),
        "Accepted as revision %lu",
        controller->revisions->active_revision(controller->revisions_context));
    return 1;
}

static int reject_preview(morph_authoring_controller *controller, char *error)
{
    int rejected = controller->agent_preview_active &&
        controller->modules->compile_candidate(
            controller->modules_context,
            controller->agent->source_before_path(controller->agent_context),
            error,
            MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY) &&
        controller->modules->activate_candidate_with_state(
            controller->modules_context,
            controller->runtime_host,
            controller->preview_restore_state,
            controller->preview_restore_state_size,
            error,
            MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY);
    if (!rejected) return 0;
    (void)controller->revisions->refresh_session(
        controller->revisions_context,
        error,
        MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY);
    (void)controller->agent->record_outcome(
        controller->agent_context,
        "rejected",
        controller->revisions->active_revision(controller->revisions_context),
        error,
        MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY);
    free(controller->preview_restore_state);
    controller->preview_restore_state = NULL;
    controller->preview_restore_state_size = 0;
    controller->agent_preview_active = 0;
    set_text(controller->agent_status, sizeof(controller->agent_status),
        "Preview rejected; accepted revision restored");
    return 1;
}

static int toggle_provider(morph_authoring_controller *controller, char *error)
{
    if (controller->agent_provider_is_custom) return 0;
    controller->agent_uses_ollama = !controller->agent_uses_ollama;
    set_text(controller->agent_provider_path, sizeof(controller->agent_provider_path),
        controller->agent_uses_ollama
            ? controller->ollama_provider_path
            : controller->codex_provider_path);
    if (!initialize_agent(controller, error)) return 0;
    snprintf(controller->agent_status, sizeof(controller->agent_status),
        "%s provider ready", controller->agent_uses_ollama ? "Ollama" : "Codex");
    return 1;
}

static int start_export(
    morph_authoring_controller *controller,
    const char *output_path,
    char *error)
{
    morph_authoring_project_info project = {0};
    const char *name = controller->modules->active_name(controller->modules_context);
    char bundle_identifier[256];
    if (!output_path || !*output_path) {
        set_text(error, MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY,
            "Choose an export output path");
        return 0;
    }
    if (controller->projects_enabled && controller->projects) {
        (void)controller->projects->project(
            controller->projects_context,
            controller->projects->active_index(controller->projects_context),
            &project);
        if (project.name[0]) name = project.name;
    }
    snprintf(bundle_identifier, sizeof(bundle_identifier), "dev.morpheus.%s",
        project.slug[0] ? project.slug : "generated-app");
    controller->export_service->reset(controller->export_context);
    if (!controller->export_service->start(
            controller->export_context,
            controller->source_path,
            output_path,
            name && *name ? name : "Generated Application",
            bundle_identifier,
            "1.0.0",
            error,
            MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY)) return 0;
    set_text(controller->export_status_text, sizeof(controller->export_status_text),
        "Export running");
    return 1;
}

static void poll_agent(morph_authoring_controller *controller)
{
    char error[MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY] = {0};
    char ignored[MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY] = {0};
    int finished = 0;
    int succeeded = 0;
    if (controller->agent->status(controller->agent_context) !=
        MORPHEUS_AUTHORING_AGENT_RUNNING) return;
    if (!controller->agent->poll(controller->agent_context, &finished,
            error, sizeof(error))) {
        set_text(controller->agent_status, sizeof(controller->agent_status),
            "Agent process monitoring failed");
        set_text(controller->diagnostics, sizeof(controller->diagnostics), error);
        return;
    }
    if (!finished) return;
    if (controller->agent->status(controller->agent_context) ==
        MORPHEUS_AUTHORING_AGENT_PROVIDER_SUCCEEDED) {
        succeeded = controller->agent->candidate_changed(
                controller->agent_context, error, sizeof(error)) &&
            controller->modules->compile_candidate(
                controller->modules_context,
                controller->agent->candidate_path(controller->agent_context),
                error,
                sizeof(error)) &&
            copy_runtime_state(controller, error);
        if (succeeded && controller->revision_store_ready) {
            succeeded = controller->revisions->end_session(
                controller->revisions_context, ignored, sizeof(ignored));
        }
        if (succeeded) succeeded = controller->modules->activate_candidate(
            controller->modules_context,
            controller->runtime_host,
            error,
            sizeof(error));
        (void)controller->agent->record_build(
            controller->agent_context,
            succeeded,
            controller->modules->stage_name(
                controller->modules_context,
                controller->modules->last_stage(controller->modules_context)),
            error,
            ignored,
            sizeof(ignored));
        (void)controller->agent->create_patch(
            controller->agent_context, ignored, sizeof(ignored));
    } else if (!controller->agent->read_provider_log(
            controller->agent_context, error, sizeof(error))) {
        set_text(error, sizeof(error), "External coding agent exited unsuccessfully");
    }
    if (succeeded) {
        controller->agent_preview_active = 1;
        set_text(controller->agent_status, sizeof(controller->agent_status),
            "Preview ready — accept or reject");
        controller->diagnostics[0] = '\0';
        return;
    }
    free(controller->preview_restore_state);
    controller->preview_restore_state = NULL;
    controller->preview_restore_state_size = 0;
    if (controller->revision_store_ready) {
        (void)controller->revisions->refresh_session(
            controller->revisions_context, ignored, sizeof(ignored));
    }
    if (controller->agent->status(controller->agent_context) ==
            MORPHEUS_AUTHORING_AGENT_PROVIDER_SUCCEEDED &&
        controller->agent->attempt(controller->agent_context) <
            MORPHEUS_AUTHORING_AGENT_MAX_ATTEMPTS &&
        controller->agent->start_attempt(
            controller->agent_context, error, ignored, sizeof(ignored))) {
        snprintf(controller->agent_status, sizeof(controller->agent_status),
            "Diagnostics returned; repair attempt %u of %d running",
            controller->agent->attempt(controller->agent_context),
            MORPHEUS_AUTHORING_AGENT_MAX_ATTEMPTS);
        controller->diagnostics[0] = '\0';
    } else {
        (void)controller->agent->record_outcome(
            controller->agent_context,
            "failed",
            controller->revision_store_ready
                ? controller->revisions->active_revision(controller->revisions_context)
                : 0,
            ignored,
            sizeof(ignored));
        snprintf(controller->agent_status, sizeof(controller->agent_status),
            "Agent run failed after %u attempt(s)",
            controller->agent->attempt(controller->agent_context));
        set_text(controller->diagnostics, sizeof(controller->diagnostics), error);
    }
}

static void poll_export(morph_authoring_controller *controller)
{
    char error[MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY] = {0};
    int finished = 0;
    if (controller->export_service->status(controller->export_context) !=
        MORPHEUS_AUTHORING_EXPORT_RUNNING) return;
    if (!controller->export_service->poll(
            controller->export_context, &finished, error, sizeof(error))) {
        set_text(controller->export_status_text,
            sizeof(controller->export_status_text), error);
        return;
    }
    if (!finished) return;
    if (controller->export_service->status(controller->export_context) ==
        MORPHEUS_AUTHORING_EXPORT_SUCCEEDED) {
        snprintf(controller->export_status_text,
            sizeof(controller->export_status_text), "Exported: %.450s",
            controller->export_service->output_path(controller->export_context));
    } else if (!controller->export_service->read_log(
            controller->export_context,
            controller->export_status_text,
            sizeof(controller->export_status_text))) {
        set_text(controller->export_status_text,
            sizeof(controller->export_status_text), "Export failed");
    }
}

static int snapshot_api(void *context, morph_authoring_snapshot *snapshot)
{
    morph_authoring_controller *controller = context;
    unsigned long struct_size;
    unsigned int index;
    int idle;
    if (!controller || !snapshot ||
        snapshot->struct_size < sizeof(*snapshot)) return 0;
    struct_size = snapshot->struct_size;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->struct_size = struct_size;
    snapshot->runtime_active = controller->modules->is_active(
        controller->modules_context);
    snapshot->commands_enabled = controller->started;
    snapshot->command_pending =
        controller->pending_request.command != MORPHEUS_AUTHORING_COMMAND_NONE;
    idle = workflow_idle(controller);
    snapshot->can_recompile = idle && !controller->agent_preview_active;
    snapshot->can_rollback = snapshot->can_recompile &&
        controller->revision_store_ready &&
        controller->revisions->active_revision(controller->revisions_context) > 1;
    snapshot->projects_enabled = controller->projects_enabled;
    if (controller->projects_enabled && controller->projects) {
        snapshot->project_count = controller->projects->count(
            controller->projects_context);
        if (snapshot->project_count > MORPHEUS_AUTHORING_MAX_PROJECTS) {
            snapshot->project_count = MORPHEUS_AUTHORING_MAX_PROJECTS;
        }
        snapshot->active_project_index = controller->projects->active_index(
            controller->projects_context);
        for (index = 0; index < snapshot->project_count; ++index) {
            if (!controller->projects->project(
                    controller->projects_context,
                    index,
                    &snapshot->projects[index])) {
                snapshot->project_count = index;
                break;
            }
        }
    }
    snapshot->recovered_from_crash = controller->recovered_from_crash;
    snapshot->agent_ready = controller->agent_ready;
    snapshot->agent_running = controller->agent->status(controller->agent_context) ==
        MORPHEUS_AUTHORING_AGENT_RUNNING;
    snapshot->agent_preview_active = controller->agent_preview_active;
    snapshot->agent_provider_is_custom = controller->agent_provider_is_custom;
    snapshot->agent_uses_ollama = controller->agent_uses_ollama;
    snapshot->can_change_project = idle && !controller->agent_preview_active;
    snapshot->can_toggle_agent_provider = snapshot->can_change_project &&
        !controller->agent_provider_is_custom;
    snapshot->can_submit_agent = idle && controller->agent_ready &&
        !controller->agent_preview_active;
    snapshot->can_accept_preview = idle && controller->agent_preview_active;
    snapshot->can_reject_preview = snapshot->can_accept_preview;
    snapshot->can_start_export = idle && snapshot->runtime_active &&
        !controller->agent_preview_active;
    snapshot->can_cancel_export =
        controller->export_service->status(controller->export_context) ==
            MORPHEUS_AUTHORING_EXPORT_RUNNING;
    snapshot->export_status = controller->export_service->status(
        controller->export_context);
    snapshot->active_revision = controller->revision_store_ready
        ? controller->revisions->active_revision(controller->revisions_context)
        : 0;
    set_text(snapshot->active_name, sizeof(snapshot->active_name),
        controller->modules->active_name(controller->modules_context));
    set_text(snapshot->agent_status, sizeof(snapshot->agent_status),
        controller->agent_status);
    set_text(snapshot->export_status_text, sizeof(snapshot->export_status_text),
        controller->export_status_text);
    set_text(snapshot->diagnostics, sizeof(snapshot->diagnostics),
        controller->diagnostics);
    set_text(snapshot->message, sizeof(snapshot->message), controller->message);
    return 1;
}

static int dispatch_api(
    void *context,
    const morph_authoring_request *request,
    char *error,
    unsigned long error_capacity)
{
    morph_authoring_controller *controller = context;
    int idle;
    if (!controller || !request || request->struct_size < sizeof(*request) ||
        request->command == MORPHEUS_AUTHORING_COMMAND_NONE) {
        set_text(error, error_capacity, "Invalid authoring request");
        return 0;
    }
    if (controller->pending_request.command != MORPHEUS_AUTHORING_COMMAND_NONE) {
        set_text(error, error_capacity, "Another authoring command is pending");
        return 0;
    }
    idle = workflow_idle(controller);
    switch (request->command) {
    case MORPHEUS_AUTHORING_COMMAND_RECOMPILE:
        if (!idle || controller->agent_preview_active) goto unavailable;
        break;
    case MORPHEUS_AUTHORING_COMMAND_ROLLBACK:
        if (!idle || controller->agent_preview_active ||
            !controller->revision_store_ready ||
            controller->revisions->active_revision(controller->revisions_context) <= 1)
            goto unavailable;
        break;
    case MORPHEUS_AUTHORING_COMMAND_CREATE_PROJECT:
        if (!idle || controller->agent_preview_active ||
            !controller->projects_enabled || !request->text[0]) goto unavailable;
        break;
    case MORPHEUS_AUTHORING_COMMAND_SELECT_PROJECT:
        if (!idle || controller->agent_preview_active ||
            !controller->projects_enabled || !controller->projects ||
            request->project_index >=
                controller->projects->count(controller->projects_context))
            goto unavailable;
        break;
    case MORPHEUS_AUTHORING_COMMAND_TOGGLE_AGENT_PROVIDER:
        if (!idle || controller->agent_preview_active ||
            controller->agent_provider_is_custom) goto unavailable;
        break;
    case MORPHEUS_AUTHORING_COMMAND_SUBMIT_AGENT:
        if (!idle || controller->agent_preview_active ||
            !controller->agent_ready || !request->text[0]) goto unavailable;
        break;
    case MORPHEUS_AUTHORING_COMMAND_ACCEPT_PREVIEW:
    case MORPHEUS_AUTHORING_COMMAND_REJECT_PREVIEW:
        if (!idle || !controller->agent_preview_active) goto unavailable;
        break;
    case MORPHEUS_AUTHORING_COMMAND_START_EXPORT:
        if (!idle || controller->agent_preview_active ||
            !controller->modules->is_active(controller->modules_context) ||
            !request->text[0]) goto unavailable;
        break;
    case MORPHEUS_AUTHORING_COMMAND_CANCEL_EXPORT:
        if (controller->export_service->status(controller->export_context) !=
            MORPHEUS_AUTHORING_EXPORT_RUNNING) goto unavailable;
        break;
    default:
        set_text(error, error_capacity, "Unsupported authoring command");
        return 0;
    }
    controller->pending_request = *request;
    set_text(controller->message, sizeof(controller->message), "Command queued");
    if (error && error_capacity) error[0] = '\0';
    return 1;

unavailable:
    set_text(error, error_capacity, "Authoring command is currently unavailable");
    return 0;
}

static const morph_authoring_controller_api controller_api = {
    MORPHEUS_AUTHORING_CONTROLLER_ABI_VERSION,
    sizeof(morph_authoring_controller_api),
    snapshot_api,
    dispatch_api
};

int morph_authoring_controller_init(
    morph_authoring_controller *controller,
    const morph_capability_registry *capabilities,
    morph_host *runtime_host)
{
    const morph_capability *projects;
    const morph_capability *revisions;
    const morph_capability *modules;
    const morph_capability *agent;
    const morph_capability *export_service;
    if (!controller || !capabilities || !runtime_host) return 0;
    memset(controller, 0, sizeof(*controller));
    projects = find_capability(capabilities,
        MORPHEUS_AUTHORING_PROJECTS_CAPABILITY,
        MORPHEUS_AUTHORING_PROJECTS_ABI_VERSION);
    revisions = find_capability(capabilities,
        MORPHEUS_AUTHORING_REVISIONS_CAPABILITY,
        MORPHEUS_AUTHORING_REVISIONS_ABI_VERSION);
    modules = find_capability(capabilities,
        MORPHEUS_AUTHORING_MODULES_CAPABILITY,
        MORPHEUS_AUTHORING_MODULES_ABI_VERSION);
    agent = find_capability(capabilities,
        MORPHEUS_AUTHORING_AGENT_CAPABILITY,
        MORPHEUS_AUTHORING_AGENT_ABI_VERSION);
    export_service = find_capability(capabilities,
        MORPHEUS_AUTHORING_EXPORT_CAPABILITY,
        MORPHEUS_AUTHORING_EXPORT_ABI_VERSION);
    controller->projects = morph_authoring_projects_from_capability(projects);
    controller->projects_context = projects ? projects->context : NULL;
    controller->revisions = morph_authoring_revisions_from_capability(revisions);
    controller->revisions_context = revisions ? revisions->context : NULL;
    controller->modules = morph_authoring_modules_from_capability(modules);
    controller->modules_context = modules ? modules->context : NULL;
    controller->agent = morph_authoring_agent_from_capability(agent);
    controller->agent_context = agent ? agent->context : NULL;
    controller->export_service = morph_authoring_export_from_capability(export_service);
    controller->export_context = export_service ? export_service->context : NULL;
    controller->runtime_host = runtime_host;
    if (!controller->agent) controller->agent = &unavailable_agent;
    if (!controller->export_service) controller->export_service = &unavailable_export;
    set_text(controller->message, sizeof(controller->message), "Authoring ready");
    set_text(controller->agent_status, sizeof(controller->agent_status), "Agent ready");
    return controller->revisions && controller->modules;
}

int morph_authoring_controller_start(
    morph_authoring_controller *controller,
    const morph_authoring_controller_config *config,
    char *error,
    unsigned long error_capacity)
{
    char local_error[MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY] = {0};
    if (!controller || !config || !config->workspace_root ||
        !config->source_path) return 0;
    controller->projects_enabled = config->projects_enabled && controller->projects;
    controller->agent_provider_is_custom = config->agent_provider_is_custom;
    controller->agent_uses_ollama = config->agent_uses_ollama;
    set_text(controller->workspace_root, sizeof(controller->workspace_root),
        config->workspace_root);
    set_text(controller->source_path, sizeof(controller->source_path),
        config->source_path);
    set_text(controller->assets_root, sizeof(controller->assets_root),
        config->assets_root);
    set_text(controller->agent_provider_path, sizeof(controller->agent_provider_path),
        config->agent_provider_path);
    set_text(controller->codex_provider_path, sizeof(controller->codex_provider_path),
        config->codex_provider_path);
    set_text(controller->ollama_provider_path, sizeof(controller->ollama_provider_path),
        config->ollama_provider_path);
    set_text(controller->ollama_model, sizeof(controller->ollama_model),
        config->ollama_model);
    set_text(controller->api_header_path, sizeof(controller->api_header_path),
        config->api_header_path);
    set_text(controller->sdk_header_path, sizeof(controller->sdk_header_path),
        config->sdk_header_path);
    controller->modules->init(controller->modules_context);
    controller->export_service->reset(controller->export_context);
    controller->started = 1;
    if (!load_current_project(controller, local_error)) {
        set_text(error, error_capacity, local_error);
        return 0;
    }
    if (error && error_capacity) error[0] = '\0';
    return 1;
}

int morph_authoring_controller_has_pending(
    const morph_authoring_controller *controller)
{
    return controller &&
        controller->pending_request.command != MORPHEUS_AUTHORING_COMMAND_NONE;
}

void morph_authoring_controller_set_availability(
    morph_authoring_controller *controller,
    int revision_store_ready,
    int workflow_is_idle)
{
    (void)workflow_is_idle;
    if (!controller) return;
    controller->revision_store_ready = revision_store_ready;
    controller->started = 1;
}

int morph_authoring_controller_tick(morph_authoring_controller *controller)
{
    morph_authoring_request request;
    char error[MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY] = {0};
    int succeeded = 1;
    if (!controller || !controller->started) return 0;
    request = controller->pending_request;
    controller->pending_request.command = MORPHEUS_AUTHORING_COMMAND_NONE;
    switch (request.command) {
    case MORPHEUS_AUTHORING_COMMAND_NONE:
        break;
    case MORPHEUS_AUTHORING_COMMAND_RECOMPILE:
        succeeded = recompile(controller, error);
        break;
    case MORPHEUS_AUTHORING_COMMAND_ROLLBACK:
        succeeded = rollback(controller, error);
        break;
    case MORPHEUS_AUTHORING_COMMAND_CREATE_PROJECT:
        succeeded = controller->projects_enabled && controller->projects->create(
            controller->projects_context, request.text, error, sizeof(error));
        if (succeeded) succeeded = switch_project(
            controller,
            controller->projects->active_index(controller->projects_context),
            error);
        break;
    case MORPHEUS_AUTHORING_COMMAND_SELECT_PROJECT:
        succeeded = switch_project(controller, request.project_index, error);
        break;
    case MORPHEUS_AUTHORING_COMMAND_TOGGLE_AGENT_PROVIDER:
        succeeded = toggle_provider(controller, error);
        break;
    case MORPHEUS_AUTHORING_COMMAND_SUBMIT_AGENT:
        succeeded = submit_agent(controller, &request, error);
        break;
    case MORPHEUS_AUTHORING_COMMAND_ACCEPT_PREVIEW:
        succeeded = accept_preview(controller, error);
        break;
    case MORPHEUS_AUTHORING_COMMAND_REJECT_PREVIEW:
        succeeded = reject_preview(controller, error);
        break;
    case MORPHEUS_AUTHORING_COMMAND_START_EXPORT:
        succeeded = start_export(controller, request.text, error);
        break;
    case MORPHEUS_AUTHORING_COMMAND_CANCEL_EXPORT:
        controller->export_service->cancel(controller->export_context);
        set_text(controller->export_status_text,
            sizeof(controller->export_status_text), "Export cancelled");
        break;
    default:
        succeeded = 0;
        set_text(error, sizeof(error), "Unsupported authoring command");
        break;
    }
    if (request.command != MORPHEUS_AUTHORING_COMMAND_NONE) {
        if (succeeded && request.command == MORPHEUS_AUTHORING_COMMAND_RECOMPILE) {
            set_text(controller->message, sizeof(controller->message),
                "Recompile succeeded");
        } else if (succeeded &&
            request.command == MORPHEUS_AUTHORING_COMMAND_ROLLBACK) {
            set_text(controller->message, sizeof(controller->message),
                "Rollback succeeded");
        } else {
            set_text(controller->message, sizeof(controller->message),
                succeeded ? "Command succeeded" : "Command failed");
        }
        set_text(controller->diagnostics, sizeof(controller->diagnostics),
            succeeded ? "" : error);
    }
    poll_agent(controller);
    poll_export(controller);
    return succeeded;
}

void morph_authoring_controller_update_runtime(
    morph_authoring_controller *controller,
    double dt)
{
    if (controller && controller->started) {
        controller->modules->update(
            controller->modules_context, controller->runtime_host, dt);
    }
}

unsigned int morph_authoring_controller_render_mode(
    const morph_authoring_controller *controller)
{
    return controller && controller->started
        ? controller->modules->render_mode(controller->modules_context)
        : MORPHEUS_RENDER_EMBEDDED;
}

void morph_authoring_controller_render_runtime(
    morph_authoring_controller *controller)
{
    if (controller && controller->started) {
        controller->modules->render_ui(
            controller->modules_context, controller->runtime_host);
    }
}

void morph_authoring_controller_shutdown(morph_authoring_controller *controller)
{
    char ignored[MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY];
    if (!controller || !controller->started) return;
    controller->agent->cancel(controller->agent_context);
    controller->export_service->cancel(controller->export_context);
    free(controller->preview_restore_state);
    controller->preview_restore_state = NULL;
    controller->preview_restore_state_size = 0;
    controller->modules->destroy(
        controller->modules_context, controller->runtime_host);
    if (controller->revision_store_ready) {
        (void)controller->revisions->end_session(
            controller->revisions_context, ignored, sizeof(ignored));
    }
    controller->started = 0;
}

morph_capability morph_authoring_controller_capability(
    morph_authoring_controller *controller)
{
    morph_capability capability = {
        MORPHEUS_AUTHORING_CONTROLLER_CAPABILITY,
        MORPHEUS_AUTHORING_CONTROLLER_ABI_VERSION,
        sizeof(controller_api),
        &controller_api,
        controller
    };
    return capability;
}
