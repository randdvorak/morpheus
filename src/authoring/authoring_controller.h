#ifndef MORPHEUS_AUTHORING_CONTROLLER_H
#define MORPHEUS_AUTHORING_CONTROLLER_H

#include "morpheus/authoring.h"

typedef struct morph_authoring_controller_config {
    int projects_enabled;
    const char *workspace_root;
    const char *source_path;
    const char *assets_root;
    const char *agent_provider_path;
    const char *codex_provider_path;
    const char *ollama_provider_path;
    const char *ollama_model;
    const char *api_header_path;
    const char *sdk_header_path;
    int agent_provider_is_custom;
    int agent_uses_ollama;
} morph_authoring_controller_config;

typedef struct morph_authoring_controller {
    const morph_authoring_projects_api *projects;
    void *projects_context;
    const morph_authoring_revisions_api *revisions;
    void *revisions_context;
    const morph_authoring_modules_api *modules;
    void *modules_context;
    const morph_authoring_agent_api *agent;
    void *agent_context;
    const morph_authoring_export_api *export_service;
    void *export_context;
    morph_host *runtime_host;
    int projects_enabled;
    int revision_store_ready;
    int recovered_from_crash;
    int agent_ready;
    int agent_preview_active;
    int agent_provider_is_custom;
    int agent_uses_ollama;
    int started;
    morph_authoring_request pending_request;
    void *preview_restore_state;
    unsigned long preview_restore_state_size;
    char workspace_root[MORPHEUS_AUTHORING_CONTROLLER_PATH_CAPACITY];
    char source_path[MORPHEUS_AUTHORING_CONTROLLER_PATH_CAPACITY];
    char assets_root[MORPHEUS_AUTHORING_CONTROLLER_PATH_CAPACITY];
    char agent_root[MORPHEUS_AUTHORING_CONTROLLER_PATH_CAPACITY];
    char agent_provider_path[MORPHEUS_AUTHORING_CONTROLLER_PATH_CAPACITY];
    char codex_provider_path[MORPHEUS_AUTHORING_CONTROLLER_PATH_CAPACITY];
    char ollama_provider_path[MORPHEUS_AUTHORING_CONTROLLER_PATH_CAPACITY];
    char ollama_model[MORPHEUS_AUTHORING_AGENT_MODEL_CAPACITY];
    char api_header_path[MORPHEUS_AUTHORING_CONTROLLER_PATH_CAPACITY];
    char sdk_header_path[MORPHEUS_AUTHORING_CONTROLLER_PATH_CAPACITY];
    char message[MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY];
    char agent_status[MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY];
    char export_status_text[MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY];
    char diagnostics[MORPHEUS_AUTHORING_CONTROLLER_MESSAGE_CAPACITY];
} morph_authoring_controller;

int morph_authoring_controller_init(
    morph_authoring_controller *controller,
    const morph_capability_registry *capabilities,
    morph_host *runtime_host);
int morph_authoring_controller_start(
    morph_authoring_controller *controller,
    const morph_authoring_controller_config *config,
    char *error,
    unsigned long error_capacity);
void morph_authoring_controller_set_availability(
    morph_authoring_controller *controller,
    int revision_store_ready,
    int workflow_is_idle);
int morph_authoring_controller_has_pending(
    const morph_authoring_controller *controller);
int morph_authoring_controller_tick(morph_authoring_controller *controller);
void morph_authoring_controller_update_runtime(
    morph_authoring_controller *controller,
    double dt);
unsigned int morph_authoring_controller_render_mode(
    const morph_authoring_controller *controller);
void morph_authoring_controller_render_runtime(
    morph_authoring_controller *controller);
void morph_authoring_controller_shutdown(
    morph_authoring_controller *controller);
morph_capability morph_authoring_controller_capability(
    morph_authoring_controller *controller);

#endif
