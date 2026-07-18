#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "revision_store.h"

static int write_source(const char *path, const char *source)
{
    FILE *file = fopen(path, "wb");
    size_t size = strlen(source);
    int write_failed;
    int close_failed;
    if (!file) {
        return 0;
    }
    write_failed = fwrite(source, 1, size, file) != size;
    close_failed = fclose(file) != 0;
    if (write_failed || close_failed) {
        return 0;
    }
    return 1;
}

static int read_source(const char *path, char *buffer, size_t capacity)
{
    FILE *file = fopen(path, "rb");
    size_t size;
    if (!file) {
        return 0;
    }
    size = fread(buffer, 1, capacity - 1, file);
    fclose(file);
    buffer[size] = '\0';
    return 1;
}

static void cleanup_store(const char *root, unsigned long latest)
{
    char path[MORPH_REVISION_PATH_CAPACITY];
    unsigned long revision;
    const char *files[] = {"app.c", "state.bin", "revision.json"};
    size_t index;

    for (revision = 1; revision <= latest; ++revision) {
        for (index = 0; index < sizeof(files) / sizeof(files[0]); ++index) {
            snprintf(
                path,
                sizeof(path),
                "%s/revisions/%08lu/%s",
                root,
                revision,
                files[index]);
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
    char root[] = "/private/tmp/morpheus-revisions-XXXXXX";
    char source_path[MORPH_REVISION_PATH_CAPACITY];
    char loaded_source_path[MORPH_REVISION_PATH_CAPACITY];
    char source_buffer[64];
    struct stat asset_status;
    char error[512];
    morph_revision_store store;
    morph_revision_store reopened;
    morph_revision_store after_interruption;
    unsigned long revision;
    unsigned long previous;
    unsigned long state_size;
    void *state_data = NULL;
    int first_state = 41;
    int second_state = 84;
    int recovered_from_crash = 0;

    if (!mkdtemp(root)) {
        perror("mkdtemp");
        return 1;
    }
    snprintf(source_path, sizeof(source_path), "%s/working.c", root);

    if (!morph_revision_store_init(&store, root, error, sizeof(error)) ||
        store.active_revision != 0 ||
        store.latest_revision != 0) {
        fprintf(stderr, "store initialization failed: %s\n", error);
        cleanup_store(root, 0);
        return 2;
    }
    snprintf(loaded_source_path, sizeof(loaded_source_path), "%s/assets", root);
    if (stat(loaded_source_path, &asset_status) != 0 ||
        !S_ISDIR(asset_status.st_mode)) {
        fprintf(stderr, "asset workspace was not created\n");
        cleanup_store(root, 0);
        return 3;
    }

    if (!write_source(source_path, "version one\n") ||
        !morph_revision_store_checkpoint(
            &store,
            source_path,
            &first_state,
            sizeof(first_state),
            &revision,
            error,
            sizeof(error)) ||
        revision != 1 || store.active_revision != 1) {
        fprintf(stderr, "first checkpoint failed: %s\n", error);
        cleanup_store(root, store.latest_revision);
        return 4;
    }

    if (!write_source(source_path, "version two\n") ||
        !morph_revision_store_checkpoint(
            &store,
            source_path,
            &second_state,
            sizeof(second_state),
            &revision,
            error,
            sizeof(error)) ||
        revision != 2 ||
        !morph_revision_store_previous(&store, &previous) ||
        previous != 1) {
        fprintf(stderr, "second checkpoint failed: %s\n", error);
        cleanup_store(root, store.latest_revision);
        return 5;
    }

    if (!morph_revision_store_load(
            &store,
            previous,
            loaded_source_path,
            sizeof(loaded_source_path),
            &state_data,
            &state_size,
            error,
            sizeof(error)) ||
        state_size != sizeof(first_state) ||
        *(int *)state_data != first_state ||
        !read_source(loaded_source_path, source_buffer, sizeof(source_buffer)) ||
        strcmp(source_buffer, "version one\n") != 0) {
        fprintf(stderr, "checkpoint load failed: %s\n", error);
        morph_revision_store_release_state(state_data);
        cleanup_store(root, store.latest_revision);
        return 6;
    }
    morph_revision_store_release_state(state_data);

    if (!morph_revision_store_set_active(&store, previous, error, sizeof(error)) ||
        !morph_revision_store_record_attempt(
            &store,
            "active",
            1,
            "rollback accepted",
            error,
            sizeof(error)) ||
        !morph_revision_store_init(&reopened, root, error, sizeof(error)) ||
        reopened.active_revision != 1 ||
        reopened.latest_revision != 2) {
        fprintf(stderr, "manifest rollback failed: %s\n", error);
        cleanup_store(root, store.latest_revision);
        return 7;
    }

    if (!morph_revision_store_checkpoint(
            &reopened,
            source_path,
            &second_state,
            sizeof(second_state),
            &revision,
            error,
            sizeof(error)) ||
        revision != 3 ||
        !morph_revision_store_previous(&reopened, &previous) ||
        previous != 1) {
        fprintf(stderr, "revision sequence was overwritten: %s\n", error);
        cleanup_store(root, reopened.latest_revision);
        return 8;
    }

    snprintf(
        loaded_source_path,
        sizeof(loaded_source_path),
        "%s/revisions/%08lu",
        root,
        4UL);
    if (mkdir(loaded_source_path, 0755) != 0) {
        fprintf(stderr, "unable to create interrupted checkpoint fixture\n");
        cleanup_store(root, reopened.latest_revision);
        return 9;
    }
    snprintf(
        loaded_source_path,
        sizeof(loaded_source_path),
        "%s/revisions/%08lu/app.c",
        root,
        4UL);
    if (!write_source(loaded_source_path, "partial\n") ||
        !morph_revision_store_init(
            &after_interruption,
            root,
            error,
            sizeof(error)) ||
        after_interruption.latest_revision != 3 ||
        !morph_revision_store_checkpoint(
            &after_interruption,
            source_path,
            &second_state,
            sizeof(second_state),
            &revision,
            error,
            sizeof(error)) ||
        revision != 4) {
        fprintf(stderr, "interrupted checkpoint recovery failed: %s\n", error);
        cleanup_store(root, 4);
        return 10;
    }

    if (!morph_revision_store_begin_session(
            &after_interruption,
            &recovered_from_crash,
            error,
            sizeof(error)) ||
        recovered_from_crash ||
        !morph_revision_store_init(&reopened, root, error, sizeof(error)) ||
        !morph_revision_store_begin_session(
            &reopened,
            &recovered_from_crash,
            error,
            sizeof(error)) ||
        !recovered_from_crash ||
        reopened.active_revision != 3 ||
        !morph_revision_store_end_session(
            &reopened,
            error,
            sizeof(error))) {
        fprintf(stderr, "crash recovery failed: %s\n", error);
        cleanup_store(root, after_interruption.latest_revision);
        return 11;
    }

    cleanup_store(root, after_interruption.latest_revision);
    puts("PASS: checkpoints, rollback, crash recovery, assets, and history");
    return 0;
}
