#include "revision_store.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static int morph_store_error(
    char *error,
    unsigned long error_capacity,
    const char *message)
{
    if (error && error_capacity) {
        snprintf(error, (size_t)error_capacity, "%s", message);
    }
    return 0;
}

static int morph_store_path(
    char *output,
    unsigned long capacity,
    const char *format,
    const char *root,
    unsigned long revision)
{
    int written = snprintf(output, (size_t)capacity, format, root, revision);
    return written >= 0 && (unsigned long)written < capacity;
}

static int morph_store_mkdir(
    const char *path,
    char *error,
    unsigned long error_capacity)
{
    if (mkdir(path, 0755) == 0 || errno == EEXIST) {
        return 1;
    }
    return morph_store_error(error, error_capacity, strerror(errno));
}

static int morph_store_write_atomic(
    const char *path,
    const void *data,
    unsigned long size,
    char *error,
    unsigned long error_capacity)
{
    char temporary[MORPH_REVISION_PATH_CAPACITY];
    FILE *file;
    int written;

    written = snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    if (written < 0 || (unsigned long)written >= sizeof(temporary)) {
        return morph_store_error(error, error_capacity, "Temporary path is too long");
    }

    file = fopen(temporary, "wb");
    if (!file) {
        return morph_store_error(error, error_capacity, strerror(errno));
    }
    if (size && fwrite(data, 1, (size_t)size, file) != (size_t)size) {
        fclose(file);
        remove(temporary);
        return morph_store_error(error, error_capacity, "Unable to write revision data");
    }
    if (fflush(file) != 0 || fclose(file) != 0) {
        remove(temporary);
        return morph_store_error(error, error_capacity, "Unable to flush revision data");
    }
    if (rename(temporary, path) != 0) {
        remove(temporary);
        return morph_store_error(error, error_capacity, strerror(errno));
    }
    return 1;
}

static int morph_store_copy_file(
    const char *source_path,
    const char *destination_path,
    char *error,
    unsigned long error_capacity)
{
    FILE *source;
    FILE *destination;
    unsigned char buffer[16384];
    size_t count;
    int source_failed;
    int source_close_failed;
    int destination_close_failed;

    source = fopen(source_path, "rb");
    if (!source) {
        return morph_store_error(error, error_capacity, strerror(errno));
    }
    destination = fopen(destination_path, "wb");
    if (!destination) {
        fclose(source);
        return morph_store_error(error, error_capacity, strerror(errno));
    }

    while ((count = fread(buffer, 1, sizeof(buffer), source)) != 0) {
        if (fwrite(buffer, 1, count, destination) != count) {
            fclose(source);
            fclose(destination);
            return morph_store_error(error, error_capacity, "Unable to copy revision source");
        }
    }
    source_failed = ferror(source);
    source_close_failed = fclose(source) != 0;
    destination_close_failed = fclose(destination) != 0;
    if (source_failed || source_close_failed || destination_close_failed) {
        return morph_store_error(error, error_capacity, "Unable to finish revision source copy");
    }
    return 1;
}

static int morph_store_write_manifest(
    morph_revision_store *store,
    unsigned long revision,
    char *error,
    unsigned long error_capacity)
{
    char path[MORPH_REVISION_PATH_CAPACITY];
    char manifest[256];
    int length;

    if (!morph_store_path(
            path,
            sizeof(path),
            "%s/app.json",
            store->workspace_root,
            0)) {
        return morph_store_error(error, error_capacity, "Manifest path is too long");
    }
    length = snprintf(
        manifest,
        sizeof(manifest),
        "{\n"
        "  \"schema_version\": 1,\n"
        "  \"active_revision\": %lu,\n"
        "  \"assets_path\": \"assets\"\n"
        "}\n",
        revision);
    if (length < 0 || (unsigned long)length >= sizeof(manifest)) {
        return morph_store_error(error, error_capacity, "Manifest is too large");
    }
    if (!morph_store_write_atomic(
            path,
            manifest,
            (unsigned long)length,
            error,
            error_capacity)) {
        return 0;
    }
    store->active_revision = revision;
    return 1;
}

static unsigned long morph_store_read_active(const char *root)
{
    char path[MORPH_REVISION_PATH_CAPACITY];
    char buffer[512];
    char *field;
    FILE *file;
    size_t size;
    unsigned long revision = 0;

    if (!morph_store_path(path, sizeof(path), "%s/app.json", root, 0)) {
        return 0;
    }
    file = fopen(path, "rb");
    if (!file) {
        return 0;
    }
    size = fread(buffer, 1, sizeof(buffer) - 1, file);
    fclose(file);
    buffer[size] = '\0';
    field = strstr(buffer, "\"active_revision\"");
    if (field) {
        field = strchr(field, ':');
        if (field) {
            (void)sscanf(field + 1, "%lu", &revision);
        }
    }
    return revision;
}

static int morph_store_revision_complete(
    const char *root,
    unsigned long revision)
{
    const char *files[] = {"app.c", "state.bin", "revision.json"};
    char path[MORPH_REVISION_PATH_CAPACITY];
    struct stat status;
    size_t index;

    for (index = 0; index < sizeof(files) / sizeof(files[0]); ++index) {
        int written = snprintf(
            path,
            sizeof(path),
            "%s/revisions/%08lu/%s",
            root,
            revision,
            files[index]);
        if (written < 0 || (unsigned long)written >= sizeof(path) ||
            stat(path, &status) != 0 || !S_ISREG(status.st_mode)) {
            return 0;
        }
    }
    return 1;
}

static unsigned long morph_store_find_latest(const char *root)
{
    char path[MORPH_REVISION_PATH_CAPACITY];
    DIR *directory;
    struct dirent *entry;
    unsigned long latest = 0;

    if (!morph_store_path(path, sizeof(path), "%s/revisions", root, 0)) {
        return 0;
    }
    directory = opendir(path);
    if (!directory) {
        return 0;
    }
    while ((entry = readdir(directory)) != NULL) {
        const char *cursor = entry->d_name;
        unsigned long revision = 0;
        if (!*cursor) {
            continue;
        }
        while (*cursor && isdigit((unsigned char)*cursor)) {
            revision = revision * 10 + (unsigned long)(*cursor - '0');
            cursor += 1;
        }
        if (!*cursor && revision > latest &&
            morph_store_revision_complete(root, revision)) {
            latest = revision;
        }
    }
    closedir(directory);
    return latest;
}

static unsigned long morph_store_read_parent(
    const morph_revision_store *store,
    unsigned long revision)
{
    char path[MORPH_REVISION_PATH_CAPACITY];
    char buffer[512];
    char *field;
    FILE *file;
    size_t size;
    unsigned long parent = 0;

    if (!morph_store_path(
            path,
            sizeof(path),
            "%s/revisions/%08lu/revision.json",
            store->workspace_root,
            revision)) {
        return 0;
    }
    file = fopen(path, "rb");
    if (!file) {
        return 0;
    }
    size = fread(buffer, 1, sizeof(buffer) - 1, file);
    fclose(file);
    buffer[size] = '\0';
    field = strstr(buffer, "\"parent_revision\"");
    if (field) {
        field = strchr(field, ':');
        if (field) {
            (void)sscanf(field + 1, "%lu", &parent);
        }
    }
    return parent;
}

void morph_revision_store_reset(morph_revision_store *store)
{
    memset(store, 0, sizeof(*store));
}

int morph_revision_store_init(
    morph_revision_store *store,
    const char *workspace_root,
    char *error,
    unsigned long error_capacity)
{
    char revisions[MORPH_REVISION_PATH_CAPACITY];
    char assets[MORPH_REVISION_PATH_CAPACITY];
    int written;

    morph_revision_store_reset(store);
    if (error && error_capacity) {
        error[0] = '\0';
    }
    written = snprintf(store->workspace_root, sizeof(store->workspace_root), "%s", workspace_root);
    if (written < 0 || (unsigned long)written >= sizeof(store->workspace_root)) {
        return morph_store_error(error, error_capacity, "Workspace path is too long");
    }
    if (!morph_store_mkdir(store->workspace_root, error, error_capacity) ||
        !morph_store_path(
            revisions,
            sizeof(revisions),
            "%s/revisions",
            store->workspace_root,
            0) ||
        !morph_store_mkdir(revisions, error, error_capacity) ||
        !morph_store_path(
            assets,
            sizeof(assets),
            "%s/assets",
            store->workspace_root,
            0) ||
        !morph_store_mkdir(assets, error, error_capacity)) {
        return 0;
    }

    store->active_revision = morph_store_read_active(store->workspace_root);
    store->latest_revision = morph_store_find_latest(store->workspace_root);
    if (store->active_revision > store->latest_revision) {
        store->active_revision = 0;
    }
    if (!store->active_revision) {
        return morph_store_write_manifest(store, 0, error, error_capacity);
    }
    return 1;
}

int morph_revision_store_checkpoint(
    morph_revision_store *store,
    const char *source_path,
    const void *state_data,
    unsigned long state_size,
    unsigned long *revision,
    char *error,
    unsigned long error_capacity)
{
    char directory[MORPH_REVISION_PATH_CAPACITY];
    char source_destination[MORPH_REVISION_PATH_CAPACITY];
    char state_destination[MORPH_REVISION_PATH_CAPACITY];
    char metadata_destination[MORPH_REVISION_PATH_CAPACITY];
    char metadata[256];
    unsigned long next_revision = store->latest_revision + 1;
    unsigned long parent_revision = store->active_revision;
    int metadata_size;

    if (error && error_capacity) {
        error[0] = '\0';
    }
    if (state_size && !state_data) {
        return morph_store_error(error, error_capacity, "Checkpoint state is invalid");
    }
    if (!morph_store_path(
            directory,
            sizeof(directory),
            "%s/revisions/%08lu",
            store->workspace_root,
            next_revision) ||
        !morph_store_mkdir(directory, error, error_capacity) ||
        !morph_store_path(
            source_destination,
            sizeof(source_destination),
            "%s/revisions/%08lu/app.c",
            store->workspace_root,
            next_revision) ||
        !morph_store_path(
            state_destination,
            sizeof(state_destination),
            "%s/revisions/%08lu/state.bin",
            store->workspace_root,
            next_revision) ||
        !morph_store_path(
            metadata_destination,
            sizeof(metadata_destination),
            "%s/revisions/%08lu/revision.json",
            store->workspace_root,
            next_revision)) {
        return morph_store_error(error, error_capacity, "Revision path is too long");
    }

    if (!morph_store_copy_file(
            source_path,
            source_destination,
            error,
            error_capacity) ||
        !morph_store_write_atomic(
            state_destination,
            state_data,
            state_size,
            error,
            error_capacity)) {
        return 0;
    }
    metadata_size = snprintf(
        metadata,
        sizeof(metadata),
        "{\n"
        "  \"revision\": %lu,\n"
        "  \"parent_revision\": %lu,\n"
        "  \"state_size\": %lu,\n"
        "  \"assets_path\": \"../../../assets\"\n"
        "}\n",
        next_revision,
        parent_revision,
        state_size);
    if (metadata_size < 0 || (unsigned long)metadata_size >= sizeof(metadata) ||
        !morph_store_write_atomic(
            metadata_destination,
            metadata,
            (unsigned long)metadata_size,
            error,
            error_capacity) ||
        !morph_store_write_manifest(
            store,
            next_revision,
            error,
            error_capacity)) {
        return 0;
    }

    store->latest_revision = next_revision;
    if (revision) {
        *revision = next_revision;
    }
    return 1;
}

int morph_revision_store_load(
    const morph_revision_store *store,
    unsigned long revision,
    char *source_path,
    unsigned long source_path_capacity,
    void **state_data,
    unsigned long *state_size,
    char *error,
    unsigned long error_capacity)
{
    char state_path[MORPH_REVISION_PATH_CAPACITY];
    FILE *file;
    long length;
    void *data = NULL;

    if (error && error_capacity) {
        error[0] = '\0';
    }
    if (!source_path || !state_data || !state_size || !revision) {
        return morph_store_error(error, error_capacity, "Revision load arguments are invalid");
    }
    if (!morph_store_path(
            source_path,
            source_path_capacity,
            "%s/revisions/%08lu/app.c",
            store->workspace_root,
            revision) ||
        !morph_store_path(
            state_path,
            sizeof(state_path),
            "%s/revisions/%08lu/state.bin",
            store->workspace_root,
            revision)) {
        return morph_store_error(error, error_capacity, "Revision path is too long");
    }

    file = fopen(state_path, "rb");
    if (!file) {
        return morph_store_error(error, error_capacity, strerror(errno));
    }
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return morph_store_error(error, error_capacity, "Unable to inspect revision state");
    }
    if (length) {
        data = malloc((size_t)length);
        if (!data || fread(data, 1, (size_t)length, file) != (size_t)length) {
            free(data);
            fclose(file);
            return morph_store_error(error, error_capacity, "Unable to read revision state");
        }
    }
    fclose(file);
    *state_data = data;
    *state_size = (unsigned long)length;
    return 1;
}

int morph_revision_store_previous(
    const morph_revision_store *store,
    unsigned long *revision)
{
    unsigned long parent;

    if (!revision || !store->active_revision) {
        return 0;
    }
    parent = morph_store_read_parent(store, store->active_revision);
    if (!parent && store->active_revision > 1) {
        parent = store->active_revision - 1;
    }
    if (!parent) {
        return 0;
    }
    *revision = parent;
    return 1;
}

int morph_revision_store_set_active(
    morph_revision_store *store,
    unsigned long revision,
    char *error,
    unsigned long error_capacity)
{
    if (revision > store->latest_revision) {
        return morph_store_error(error, error_capacity, "Revision does not exist");
    }
    return morph_store_write_manifest(store, revision, error, error_capacity);
}

int morph_revision_store_refresh_session(
    const morph_revision_store *store,
    char *error,
    unsigned long error_capacity)
{
    char path[MORPH_REVISION_PATH_CAPACITY];
    char marker[64];
    int length;

    if (!morph_store_path(
            path,
            sizeof(path),
            "%s/.active-session",
            store->workspace_root,
            0)) {
        return morph_store_error(error, error_capacity, "Session marker path is too long");
    }
    length = snprintf(marker, sizeof(marker), "%lu\n", store->active_revision);
    if (length < 0 || (unsigned long)length >= sizeof(marker)) {
        return morph_store_error(error, error_capacity, "Session marker is too large");
    }
    return morph_store_write_atomic(
        path,
        marker,
        (unsigned long)length,
        error,
        error_capacity);
}

int morph_revision_store_begin_session(
    morph_revision_store *store,
    int *recovered_from_crash,
    char *error,
    unsigned long error_capacity)
{
    char path[MORPH_REVISION_PATH_CAPACITY];
    FILE *file;
    unsigned long interrupted_revision = 0;
    int interrupted = 0;

    if (recovered_from_crash) {
        *recovered_from_crash = 0;
    }
    if (!morph_store_path(
            path,
            sizeof(path),
            "%s/.active-session",
            store->workspace_root,
            0)) {
        return morph_store_error(error, error_capacity, "Session marker path is too long");
    }
    file = fopen(path, "rb");
    if (file) {
        interrupted = fscanf(file, "%lu", &interrupted_revision) == 1;
        fclose(file);
    }

    if (interrupted && interrupted_revision != 0 &&
        interrupted_revision == store->active_revision) {
        unsigned long fallback = 0;
        (void)morph_revision_store_previous(store, &fallback);
        if (!morph_store_write_manifest(
                store,
                fallback,
                error,
                error_capacity)) {
            return 0;
        }
        if (recovered_from_crash) {
            *recovered_from_crash = 1;
        }
    }
    return morph_revision_store_refresh_session(store, error, error_capacity);
}

int morph_revision_store_end_session(
    const morph_revision_store *store,
    char *error,
    unsigned long error_capacity)
{
    char path[MORPH_REVISION_PATH_CAPACITY];

    if (!morph_store_path(
            path,
            sizeof(path),
            "%s/.active-session",
            store->workspace_root,
            0)) {
        return morph_store_error(error, error_capacity, "Session marker path is too long");
    }
    if (remove(path) == 0 || errno == ENOENT) {
        return 1;
    }
    return morph_store_error(error, error_capacity, strerror(errno));
}

int morph_revision_store_record_attempt(
    const morph_revision_store *store,
    const char *stage,
    int succeeded,
    const char *message,
    char *error,
    unsigned long error_capacity)
{
    char path[MORPH_REVISION_PATH_CAPACITY];
    FILE *file;
    time_t now = time(NULL);
    int write_failed;
    int close_failed;

    if (!morph_store_path(path, sizeof(path), "%s/build.log", store->workspace_root, 0)) {
        return morph_store_error(error, error_capacity, "Build log path is too long");
    }
    file = fopen(path, "ab");
    if (!file) {
        return morph_store_error(error, error_capacity, strerror(errno));
    }
    write_failed = fprintf(
            file,
            "%lld revision=%lu stage=%s success=%d %s\n",
            (long long)now,
            store->active_revision,
            stage ? stage : "unknown",
            succeeded ? 1 : 0,
            message ? message : "") < 0;
    close_failed = fclose(file) != 0;
    if (write_failed || close_failed) {
        return morph_store_error(error, error_capacity, "Unable to append build log");
    }
    return 1;
}

void morph_revision_store_release_state(void *state_data)
{
    free(state_data);
}
