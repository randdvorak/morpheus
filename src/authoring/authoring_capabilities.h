#ifndef MORPHEUS_AUTHORING_CAPABILITIES_H
#define MORPHEUS_AUTHORING_CAPABILITIES_H

#include "morpheus/authoring.h"

typedef struct morph_agent_session morph_agent_session;
typedef struct morph_export_service morph_export_service;
typedef struct morph_project_store morph_project_store;
typedef struct morph_revision_store morph_revision_store;
typedef struct morph_runtime_module morph_runtime_module;

morph_capability morph_authoring_projects_capability(
    morph_project_store *store);
morph_capability morph_authoring_revisions_capability(
    morph_revision_store *store);
morph_capability morph_authoring_modules_capability(
    morph_runtime_module *module);
morph_capability morph_authoring_agent_capability(
    morph_agent_session *session);
morph_capability morph_authoring_export_capability(
    morph_export_service *service,
    const char *tool_path);

#endif
