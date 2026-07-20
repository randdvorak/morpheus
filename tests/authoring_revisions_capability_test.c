#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "authoring_capabilities.h"
#include "revision_store.h"

static int write_source(const char *path, const char *source)
{
    FILE *file = fopen(path, "wb");
    size_t size = strlen(source);
    int failed;
    if (!file) return 0;
    failed = fwrite(source, 1, size, file) != size;
    failed = fclose(file) != 0 || failed;
    return !failed;
}

static void cleanup(const char *root, unsigned long latest)
{
    char path[MORPHEUS_AUTHORING_REVISION_PATH_CAPACITY];
    unsigned long revision;
    const char *files[] = {"app.c", "state.bin", "revision.json"};
    size_t file_index;

    for (revision = 1; revision <= latest; ++revision) {
        for (file_index = 0; file_index < sizeof(files) / sizeof(files[0]); ++file_index) {
            snprintf(path, sizeof(path), "%s/revisions/%08lu/%s",
                root, revision, files[file_index]);
            unlink(path);
        }
        snprintf(path, sizeof(path), "%s/revisions/%08lu", root, revision);
        rmdir(path);
    }
    snprintf(path, sizeof(path), "%s/working.c", root);
    unlink(path);
    snprintf(path, sizeof(path), "%s/app.json", root);
    unlink(path);
    snprintf(path, sizeof(path), "%s/build.log", root);
    unlink(path);
    snprintf(path, sizeof(path), "%s/.active-session", root);
    unlink(path);
    snprintf(path, sizeof(path), "%s/assets", root);
    rmdir(path);
    snprintf(path, sizeof(path), "%s/revisions", root);
    rmdir(path);
    rmdir(root);
}

int main(void)
{
    char root[] = "/private/tmp/morpheus-authoring-revisions-XXXXXX";
    char source[MORPHEUS_AUTHORING_REVISION_PATH_CAPACITY];
    char loaded_source[MORPHEUS_AUTHORING_REVISION_PATH_CAPACITY];
    char error[512] = {0};
    morph_revision_store store;
    morph_capability capability;
    const morph_authoring_revisions_api *api;
    void *context;
    void *loaded_state = NULL;
    unsigned long loaded_size = 0;
    unsigned long revision = 0;
    unsigned long previous = 0;
    unsigned long latest = 0;
    int recovered = 0;
    int first_state = 12;
    int second_state = 34;
    int result = 1;

    if (!mkdtemp(root)) {
        perror("mkdtemp");
        return 1;
    }
    snprintf(source, sizeof(source), "%s/working.c", root);
    capability = morph_authoring_revisions_capability(&store);
    api = morph_authoring_revisions_from_capability(&capability);
    context = capability.context;

    if (!api ||
        !api->init(context, root, error, sizeof(error)) ||
        api->active_revision(context) != 0 ||
        !write_source(source, "first\n") ||
        !api->checkpoint(context, source, &first_state, sizeof(first_state),
            &revision, error, sizeof(error)) || revision != 1 ||
        !write_source(source, "second\n") ||
        !api->checkpoint(context, source, &second_state, sizeof(second_state),
            &revision, error, sizeof(error)) || revision != 2 ||
        api->active_revision(context) != 2 ||
        api->latest_revision(context) != 2 ||
        !api->previous(context, &previous) || previous != 1 ||
        !api->load(context, previous, loaded_source, sizeof(loaded_source),
            &loaded_state, &loaded_size, error, sizeof(error)) ||
        loaded_size != sizeof(first_state) ||
        *(int *)loaded_state != first_state ||
        !api->begin_session(context, &recovered, error, sizeof(error)) || recovered ||
        !api->init(context, root, error, sizeof(error)) ||
        !api->begin_session(context, &recovered, error, sizeof(error)) || !recovered ||
        api->active_revision(context) != 1 ||
        !api->record_attempt(context, "test", 1, "capability boundary",
            error, sizeof(error)) ||
        !api->end_session(context, error, sizeof(error))) {
        fprintf(stderr, "revision capability failed: %s\n", error);
        result = 2;
    }

    latest = api ? api->latest_revision(context) : 2;
    if (api) api->release_state(context, loaded_state);
    cleanup(root, latest);
    if (result == 1) {
        puts("PASS: revision history and recovery through authoring capability");
        return 0;
    }
    return result;
}
