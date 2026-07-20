#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "authoring_capabilities.h"
#include "project_store.h"

static void cleanup_project(const char *root, const char *slug)
{
    char path[MORPH_PROJECT_PATH_CAPACITY];
    const char *files[] = {"app.c", "project.name"};
    size_t index;
    for (index = 0; index < sizeof(files) / sizeof(files[0]); ++index) {
        snprintf(path, sizeof(path), "%s/%s/%s", root, slug, files[index]);
        unlink(path);
    }
    snprintf(path, sizeof(path), "%s/%s/assets", root, slug);
    rmdir(path);
    snprintf(path, sizeof(path), "%s/%s/agent", root, slug);
    rmdir(path);
    snprintf(path, sizeof(path), "%s/%s", root, slug);
    rmdir(path);
}

int main(void)
{
    char root[] = "/private/tmp/morpheus-authoring-projects-XXXXXX";
    char workspace[MORPH_PROJECT_PATH_CAPACITY];
    char source[MORPH_PROJECT_PATH_CAPACITY];
    char assets[MORPH_PROJECT_PATH_CAPACITY];
    char active_path[MORPH_PROJECT_PATH_CAPACITY];
    char error[512] = {0};
    morph_project_store store = {0};
    morph_authoring_project_info project = {0};
    morph_capability entry;
    morph_capability_registry registry;
    morph_host host = {0};
    const morph_capability *capability;
    const morph_authoring_projects_api *api;
    void *context;
    unsigned int active_index;
    int result = 1;

    if (!mkdtemp(root)) {
        perror("mkdtemp");
        return 1;
    }
    entry = morph_authoring_projects_capability(&store);
    registry.entries = &entry;
    registry.count = 1;
    host.abi_version = MORPHEUS_HOST_ABI_VERSION;
    host.capabilities = &registry;

    capability = morph_host_find_capability(
        &host,
        MORPHEUS_AUTHORING_PROJECTS_CAPABILITY,
        MORPHEUS_AUTHORING_PROJECTS_ABI_VERSION);
    api = morph_authoring_projects_from_capability(capability);
    context = capability ? capability->context : NULL;
    if (!api || !api->init(context, root, error, sizeof(error)) ||
        api->count(context) != 1 ||
        !api->project(context, 0, &project) ||
        strcmp(project.name, "My App") != 0 ||
        !api->create(context, "Second App", error, sizeof(error)) ||
        api->count(context) != 2) {
        fprintf(stderr, "project capability initialization failed: %s\n", error);
        result = 2;
        goto cleanup;
    }

    active_index = api->active_index(context);
    if (!api->project(context, active_index, &project) ||
        strcmp(project.name, "Second App") != 0 ||
        strcmp(project.slug, "second-app") != 0 ||
        !api->paths(context,
            workspace, sizeof(workspace),
            source, sizeof(source),
            assets, sizeof(assets)) ||
        !strstr(workspace, "/second-app") ||
        !strstr(source, "/second-app/app.c") ||
        !strstr(assets, "/second-app/assets") ||
        !api->select(context, 0, error, sizeof(error)) ||
        api->active_index(context) != 0 ||
        api->project(context, 2, &project)) {
        fprintf(stderr, "project capability operations failed: %s\n", error);
        result = 3;
        goto cleanup;
    }

    puts("PASS: project lifecycle through authoring capability");
    result = 0;

cleanup:
    cleanup_project(root, "my-app");
    cleanup_project(root, "second-app");
    snprintf(active_path, sizeof(active_path), "%s/.active-project", root);
    unlink(active_path);
    rmdir(root);
    return result;
}
