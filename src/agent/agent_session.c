#define _POSIX_C_SOURCE 200809L

#include "agent_session.h"

#include <CommonCrypto/CommonDigest.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

#define MORPH_AGENT_RESPONSE_MAX_BYTES (256UL * 1024UL)
#define MORPH_AGENT_LOG_MAX_BYTES (1024UL * 1024UL)
#define MORPH_AGENT_TREE_MAX_BYTES (32UL * 1024UL * 1024UL)
#define MORPH_AGENT_TREE_MAX_ENTRIES 4096UL
#define MORPH_AGENT_TREE_MAX_DEPTH 64U

typedef struct morph_agent_tree_state {
    const char *root;
    const char *excluded[3];
    unsigned long entries;
    unsigned long long bytes;
    uid_t owner;
} morph_agent_tree_state;

static int morph_agent_error(
    char *error,
    unsigned long error_capacity,
    const char *message)
{
    if (error && error_capacity) {
        snprintf(error, (size_t)error_capacity, "%s", message);
    }
    return 0;
}

static int morph_agent_boundary_error(
    char *error,
    unsigned long error_capacity,
    const char *message)
{
    char detail[MORPH_AGENT_FAILURE_CAPACITY];
    snprintf(detail, sizeof(detail), "Agent provider violated artifact boundary: %s", message);
    return morph_agent_error(error, error_capacity, detail);
}

static void morph_agent_digest_bytes(
    CC_SHA256_CTX *digest,
    const void *data,
    size_t size)
{
    const unsigned char *cursor = data;
    while (size) {
        CC_LONG count = size > UINT32_MAX ? UINT32_MAX : (CC_LONG)size;
        (void)CC_SHA256_Update(digest, cursor, count);
        cursor += count;
        size -= count;
    }
}

static void morph_agent_digest_u64(CC_SHA256_CTX *digest, uint64_t value)
{
    unsigned char encoded[8];
    unsigned int index;
    for (index = 0; index < sizeof(encoded); ++index) {
        encoded[sizeof(encoded) - index - 1] = (unsigned char)(value & 0xffU);
        value >>= 8;
    }
    morph_agent_digest_bytes(digest, encoded, sizeof(encoded));
}

static int morph_agent_tree_path_excluded(
    const morph_agent_tree_state *state,
    const char *path)
{
    size_t index;
    for (index = 0; index < sizeof(state->excluded) / sizeof(state->excluded[0]); ++index) {
        if (state->excluded[index] && strcmp(state->excluded[index], path) == 0) return 1;
    }
    return 0;
}

static int morph_agent_hash_tree_path(
    CC_SHA256_CTX *digest,
    morph_agent_tree_state *state,
    const char *path,
    unsigned int depth,
    char *error,
    unsigned long error_capacity)
{
    struct stat status;
    const char *relative;
    unsigned char kind;
    int excluded;

    if (depth > MORPH_AGENT_TREE_MAX_DEPTH) {
        return morph_agent_boundary_error(error, error_capacity, "run tree is too deep");
    }
    if (lstat(path, &status) != 0) {
        return morph_agent_boundary_error(error, error_capacity, "run artifact is missing");
    }
    if (++state->entries > MORPH_AGENT_TREE_MAX_ENTRIES) {
        return morph_agent_boundary_error(error, error_capacity, "run tree has too many entries");
    }
    if (status.st_uid != state->owner) {
        return morph_agent_boundary_error(error, error_capacity, "run artifact has foreign ownership");
    }
    if (S_ISLNK(status.st_mode)) {
        return morph_agent_boundary_error(error, error_capacity, "run tree contains a symbolic link");
    }
    if (!S_ISDIR(status.st_mode) && !S_ISREG(status.st_mode)) {
        return morph_agent_boundary_error(error, error_capacity, "run tree contains a special file");
    }
    if (S_ISREG(status.st_mode) && status.st_nlink != 1) {
        return morph_agent_boundary_error(error, error_capacity, "run tree contains a hard-linked file");
    }
    if (S_ISREG(status.st_mode)) {
        if (status.st_size < 0 ||
            (unsigned long long)status.st_size > MORPH_AGENT_TREE_MAX_BYTES - state->bytes) {
            return morph_agent_boundary_error(error, error_capacity, "run artifacts are too large");
        }
        state->bytes += (unsigned long long)status.st_size;
    }

    excluded = morph_agent_tree_path_excluded(state, path);
    relative = path + strlen(state->root);
    if (!*relative) relative = ".";
    else if (*relative == '/') ++relative;
    if (!excluded) {
        kind = S_ISDIR(status.st_mode) ? (unsigned char)'d' : (unsigned char)'f';
        morph_agent_digest_bytes(digest, &kind, sizeof(kind));
        morph_agent_digest_bytes(digest, relative, strlen(relative) + 1);
        morph_agent_digest_u64(digest, (uint64_t)(status.st_mode & 07777));
    }

    if (S_ISDIR(status.st_mode)) {
        struct dirent **entries = NULL;
        int count = scandir(path, &entries, NULL, alphasort);
        int index;
        int succeeded = 1;
        if (count < 0) {
            return morph_agent_boundary_error(error, error_capacity, "run directory cannot be inspected");
        }
        for (index = 0; index < count; ++index) {
            char child[MORPH_AGENT_PATH_CAPACITY];
            const char *name = entries[index]->d_name;
            int length;
            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
                free(entries[index]);
                continue;
            }
            length = snprintf(child, sizeof(child), "%s/%s", path, name);
            if (length < 0 || (unsigned long)length >= sizeof(child) ||
                !morph_agent_hash_tree_path(
                    digest, state, child, depth + 1, error, error_capacity)) {
                if (length < 0 || (unsigned long)length >= sizeof(child)) {
                    (void)morph_agent_boundary_error(
                        error, error_capacity, "run artifact path is too long");
                }
                succeeded = 0;
                free(entries[index]);
                ++index;
                break;
            }
            free(entries[index]);
        }
        while (index < count) free(entries[index++]);
        free(entries);
        return succeeded;
    }

    if (!excluded) {
        unsigned char buffer[16384];
        FILE *file = fopen(path, "rb");
        size_t count;
        if (!file) {
            return morph_agent_boundary_error(error, error_capacity, "run artifact cannot be read");
        }
        morph_agent_digest_u64(digest, (uint64_t)status.st_size);
        while ((count = fread(buffer, 1, sizeof(buffer), file)) != 0) {
            morph_agent_digest_bytes(digest, buffer, count);
        }
        {
            int failed = ferror(file);
            if (fclose(file) != 0) failed = 1;
            if (failed) {
                return morph_agent_boundary_error(
                    error, error_capacity, "run artifact cannot be hashed");
            }
        }
    }
    return 1;
}

static int morph_agent_hash_run_tree(
    const morph_agent_session *session,
    unsigned char output[MORPH_AGENT_DIGEST_SIZE],
    char *error,
    unsigned long error_capacity)
{
    CC_SHA256_CTX digest;
    morph_agent_tree_state state = {
        .root = session->run_directory,
        .excluded = {
            session->candidate_path,
            session->response_path,
            session->provider_log_path
        },
        .owner = geteuid()
    };
    if (!CC_SHA256_Init(&digest) ||
        !morph_agent_hash_tree_path(
            &digest, &state, session->run_directory, 0, error, error_capacity) ||
        !CC_SHA256_Final(output, &digest)) {
        if (error && error_capacity && !error[0]) {
            (void)morph_agent_boundary_error(error, error_capacity, "cannot hash run artifacts");
        }
        return 0;
    }
    return 1;
}

static int morph_agent_validate_output(
    const char *path,
    unsigned long max_bytes,
    int required,
    const char *description,
    char *error,
    unsigned long error_capacity)
{
    struct stat status;
    char message[256];
    if (lstat(path, &status) != 0) {
        if (!required && errno == ENOENT) return 1;
        snprintf(message, sizeof(message), "%s is missing", description);
        return morph_agent_boundary_error(error, error_capacity, message);
    }
    if (!S_ISREG(status.st_mode)) {
        snprintf(message, sizeof(message), "%s is not a regular file", description);
        return morph_agent_boundary_error(error, error_capacity, message);
    }
    if (status.st_uid != geteuid()) {
        snprintf(message, sizeof(message), "%s has foreign ownership", description);
        return morph_agent_boundary_error(error, error_capacity, message);
    }
    if (status.st_nlink != 1) {
        snprintf(message, sizeof(message), "%s is hard-linked", description);
        return morph_agent_boundary_error(error, error_capacity, message);
    }
    if (status.st_size < 0 || (unsigned long long)status.st_size > max_bytes) {
        snprintf(message, sizeof(message), "%s exceeds its size limit", description);
        return morph_agent_boundary_error(error, error_capacity, message);
    }
    return 1;
}

static int morph_agent_validate_provider_exit(
    morph_agent_session *session,
    int require_response,
    char *error,
    unsigned long error_capacity)
{
    unsigned char digest[MORPH_AGENT_DIGEST_SIZE];
    if (!session->provider_baseline_valid) {
        return morph_agent_boundary_error(error, error_capacity, "run baseline is unavailable");
    }
    if (!morph_agent_validate_output(
            session->candidate_path,
            MORPH_AGENT_CANDIDATE_MAX_BYTES,
            1,
            "candidate.c",
            error,
            error_capacity) ||
        !morph_agent_validate_output(
            session->provider_log_path,
            MORPH_AGENT_LOG_MAX_BYTES,
            1,
            "provider log",
            error,
            error_capacity) ||
        !morph_agent_validate_output(
            session->response_path,
            MORPH_AGENT_RESPONSE_MAX_BYTES,
            require_response,
            "provider response",
            error,
            error_capacity) ||
        !morph_agent_hash_run_tree(session, digest, error, error_capacity)) {
        return 0;
    }
    if (memcmp(digest, session->provider_baseline, sizeof(digest)) != 0) {
        return morph_agent_boundary_error(
            error, error_capacity, "protected or unexpected artifacts changed");
    }
    return 1;
}

static int morph_agent_path(
    char *output,
    unsigned long capacity,
    const char *format,
    const char *root,
    unsigned long number)
{
    int length = snprintf(output, (size_t)capacity, format, root, number);
    return length >= 0 && (unsigned long)length < capacity;
}

static int morph_agent_mkdir(
    const char *path,
    char *error,
    unsigned long error_capacity)
{
    struct stat status;
    if (mkdir(path, 0755) == 0) {
        return 1;
    }
    if (errno == EEXIST && stat(path, &status) == 0 && S_ISDIR(status.st_mode)) {
        return 1;
    }
    return morph_agent_error(error, error_capacity, strerror(errno));
}

static int morph_agent_write(
    const char *path,
    const void *data,
    unsigned long size,
    char *error,
    unsigned long error_capacity)
{
    char temporary[MORPH_AGENT_PATH_CAPACITY];
    FILE *file;
    int failed;
    int close_failed;
    int length = snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    if (length < 0 || (unsigned long)length >= sizeof(temporary)) {
        return morph_agent_error(error, error_capacity, "Agent artifact path is too long");
    }
    file = fopen(temporary, "wb");
    if (!file) {
        return morph_agent_error(error, error_capacity, strerror(errno));
    }
    failed = size && fwrite(data, 1, (size_t)size, file) != (size_t)size;
    if (!failed) failed = fflush(file) != 0;
    close_failed = fclose(file) != 0;
    if (failed || close_failed) {
        remove(temporary);
        return morph_agent_error(error, error_capacity, "Unable to write agent artifact");
    }
    if (rename(temporary, path) != 0) {
        remove(temporary);
        return morph_agent_error(error, error_capacity, strerror(errno));
    }
    return 1;
}

static int morph_agent_copy(
    const char *source_path,
    const char *destination_path,
    char *error,
    unsigned long error_capacity)
{
    FILE *source = fopen(source_path, "rb");
    FILE *destination;
    unsigned char buffer[16384];
    size_t count;
    int failed = 0;
    if (!source) {
        return morph_agent_error(error, error_capacity, strerror(errno));
    }
    destination = fopen(destination_path, "wb");
    if (!destination) {
        fclose(source);
        return morph_agent_error(error, error_capacity, strerror(errno));
    }
    while ((count = fread(buffer, 1, sizeof(buffer), source)) != 0) {
        if (fwrite(buffer, 1, count, destination) != count) {
            failed = 1;
            break;
        }
    }
    failed = failed || ferror(source);
    failed = fclose(source) != 0 || failed;
    failed = fclose(destination) != 0 || failed;
    if (failed) {
        return morph_agent_error(error, error_capacity, "Unable to copy agent artifact");
    }
    return 1;
}

static int morph_agent_write_prompt(
    const morph_agent_session *session,
    const char *diagnostics,
    char *error,
    unsigned long error_capacity)
{
    FILE *prompt = fopen(session->prompt_path, "wb");
    FILE *request;
    unsigned char buffer[4096];
    size_t count;
    int failed = 0;
    const char *instructions =
        "Modify candidate.c to satisfy the user request below.\n"
        "Only edit candidate.c. Read app_api.h and sdk.h for the complete contract.\n"
        "Keep the implementation freestanding C accepted by TinyCC with "
        "-nostdlib -Wall -Werror.\n"
        "Keep frames responsive: do not create large per-frame primitive grids "
        "or repeat expensive generated-raster work in render_ui. Cache bounded "
        "RGBA8 pixels with morph_image_load_rgba and redraw the resulting image; "
        "recompute only when inputs or dimensions change, splitting expensive "
        "work across bounded update steps.\n\nUser request:\n";

    if (!prompt) return morph_agent_error(error, error_capacity, strerror(errno));
    request = fopen(session->request_path, "rb");
    if (!request) {
        fclose(prompt);
        return morph_agent_error(error, error_capacity, strerror(errno));
    }
    failed = fputs(instructions, prompt) < 0;
    while (!failed && (count = fread(buffer, 1, sizeof(buffer), request)) != 0) {
        failed = fwrite(buffer, 1, count, prompt) != count;
    }
    failed = failed || ferror(request);
    if (!failed && diagnostics && *diagnostics) {
        failed = fputs(
                "\n\nCompiler or activation diagnostics from the previous attempt:\n",
                prompt) < 0 ||
            fputs(diagnostics, prompt) < 0 ||
            fputs("\nRepair every diagnostic and update candidate.c.\n", prompt) < 0;
    }
    failed = fclose(request) != 0 || failed;
    failed = fclose(prompt) != 0 || failed;
    return failed
        ? morph_agent_error(error, error_capacity, "Unable to write agent prompt")
        : 1;
}

static unsigned long morph_agent_latest_run(const char *runs_path)
{
    DIR *directory = opendir(runs_path);
    struct dirent *entry;
    unsigned long latest = 0;
    if (!directory) {
        return 0;
    }
    while ((entry = readdir(directory)) != NULL) {
        const char *cursor = entry->d_name;
        unsigned long value = 0;
        if (!*cursor) continue;
        while (*cursor && isdigit((unsigned char)*cursor)) {
            value = value * 10 + (unsigned long)(*cursor - '0');
            ++cursor;
        }
        if (!*cursor && value > latest) latest = value;
    }
    closedir(directory);
    return latest;
}

void morph_agent_session_reset(morph_agent_session *session)
{
    memset(session, 0, sizeof(*session));
}

int morph_agent_session_init(
    morph_agent_session *session,
    const char *root,
    const char *provider_path,
    char *error,
    unsigned long error_capacity)
{
    char runs[MORPH_AGENT_PATH_CAPACITY];
    int length;

    morph_agent_session_reset(session);
    length = snprintf(session->root, sizeof(session->root), "%s", root);
    if (length < 0 || (unsigned long)length >= sizeof(session->root)) {
        return morph_agent_error(error, error_capacity, "Agent workspace path is too long");
    }
    length = snprintf(
        session->provider_path,
        sizeof(session->provider_path),
        "%s",
        provider_path);
    if (length < 0 || (unsigned long)length >= sizeof(session->provider_path)) {
        return morph_agent_error(error, error_capacity, "Agent provider path is too long");
    }
    if (access(session->provider_path, X_OK) != 0) {
        return morph_agent_error(error, error_capacity, "Agent provider is not executable");
    }
    if (!morph_agent_mkdir(session->root, error, error_capacity) ||
        !morph_agent_path(runs, sizeof(runs), "%s/runs", session->root, 0) ||
        !morph_agent_mkdir(runs, error, error_capacity)) {
        return 0;
    }
    session->run_id = morph_agent_latest_run(runs);
    return 1;
}

int morph_agent_session_set_model(
    morph_agent_session *session,
    const char *model,
    char *error,
    unsigned long error_capacity)
{
    int length = snprintf(
        session->provider_model,
        sizeof(session->provider_model),
        "%s",
        model ? model : "");
    if (length < 0 || (unsigned long)length >= sizeof(session->provider_model)) {
        return morph_agent_error(error, error_capacity, "Provider model name is too long");
    }
    return 1;
}

int morph_agent_session_begin(
    morph_agent_session *session,
    const char *request,
    const char *source_path,
    const char *api_header_path,
    const char *sdk_header_path,
    char *error,
    unsigned long error_capacity)
{
    char api_destination[MORPH_AGENT_PATH_CAPACITY];
    char sdk_destination[MORPH_AGENT_PATH_CAPACITY];
    char model_destination[MORPH_AGENT_PATH_CAPACITY];

    if (session->status == MORPH_AGENT_RUNNING) {
        return morph_agent_error(error, error_capacity, "Agent provider is already running");
    }
    session->run_id += 1;
    session->attempt = 0;
    session->process_id = 0;
    session->status = MORPH_AGENT_IDLE;
    if (!morph_agent_path(
            session->run_directory,
            sizeof(session->run_directory),
            "%s/runs/%08lu",
            session->root,
            session->run_id) ||
        !morph_agent_mkdir(session->run_directory, error, error_capacity) ||
        !morph_agent_path(
            session->candidate_path,
            sizeof(session->candidate_path),
            "%s/candidate.c",
            session->run_directory,
            0) ||
        !morph_agent_path(
            session->source_before_path,
            sizeof(session->source_before_path),
            "%s/source-before.c",
            session->run_directory,
            0) ||
        !morph_agent_path(
            session->request_path,
            sizeof(session->request_path),
            "%s/request.txt",
            session->run_directory,
            0) ||
        !morph_agent_path(
            api_destination,
            sizeof(api_destination),
            "%s/app_api.h",
            session->run_directory,
            0) ||
        !morph_agent_path(
            sdk_destination,
            sizeof(sdk_destination),
            "%s/sdk.h",
            session->run_directory,
            0) ||
        !morph_agent_path(
            model_destination,
            sizeof(model_destination),
            "%s/model.txt",
            session->run_directory,
            0)) {
        return morph_agent_error(error, error_capacity, "Agent run path is too long");
    }
    return morph_agent_copy(source_path, session->source_before_path, error, error_capacity) &&
        morph_agent_copy(source_path, session->candidate_path, error, error_capacity) &&
        morph_agent_copy(api_header_path, api_destination, error, error_capacity) &&
        morph_agent_copy(sdk_header_path, sdk_destination, error, error_capacity) &&
        morph_agent_write(
            model_destination,
            session->provider_model,
            (unsigned long)strlen(session->provider_model),
            error,
            error_capacity) &&
        morph_agent_write(
            session->request_path,
            request,
            (unsigned long)strlen(request),
            error,
            error_capacity);
}

int morph_agent_session_start_attempt(
    morph_agent_session *session,
    const char *diagnostics,
    char *error,
    unsigned long error_capacity)
{
    char attempts[MORPH_AGENT_PATH_CAPACITY];
    char attempt_directory[MORPH_AGENT_PATH_CAPACITY];
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    char *arguments[6];
    int log_descriptor;
    int spawn_result;
    int attributes_ready = 0;

    if (session->status == MORPH_AGENT_RUNNING ||
        session->attempt >= MORPH_AGENT_MAX_ATTEMPTS) {
        return morph_agent_error(error, error_capacity, "Agent attempt cannot be started");
    }
    session->attempt += 1;
    session->failure_reason[0] = '\0';
    session->provider_baseline_valid = 0;
    if (!morph_agent_path(attempts, sizeof(attempts), "%s/attempts", session->run_directory, 0) ||
        !morph_agent_mkdir(attempts, error, error_capacity) ||
        !morph_agent_path(
            attempt_directory,
            sizeof(attempt_directory),
            "%s/%02lu",
            attempts,
            session->attempt) ||
        !morph_agent_mkdir(attempt_directory, error, error_capacity) ||
        !morph_agent_path(
            session->diagnostics_path,
            sizeof(session->diagnostics_path),
            "%s/diagnostics.txt",
            attempt_directory,
            0) ||
        !morph_agent_path(
            session->prompt_path,
            sizeof(session->prompt_path),
            "%s/prompt.txt",
            attempt_directory,
            0) ||
        !morph_agent_path(
            session->response_path,
            sizeof(session->response_path),
            "%s/response.txt",
            attempt_directory,
            0) ||
        !morph_agent_path(
            session->provider_log_path,
            sizeof(session->provider_log_path),
            "%s/provider.log",
            attempt_directory,
            0) ||
        !morph_agent_write(
            session->diagnostics_path,
            diagnostics ? diagnostics : "",
            diagnostics ? (unsigned long)strlen(diagnostics) : 0,
            error,
            error_capacity) ||
        !morph_agent_write_prompt(
            session,
            diagnostics,
            error,
            error_capacity)) {
        return 0;
    }

    log_descriptor = open(
        session->provider_log_path,
        O_CREAT | O_WRONLY | O_TRUNC,
        0644);
    if (log_descriptor < 0) {
        return morph_agent_error(error, error_capacity, strerror(errno));
    }
    if (!morph_agent_validate_output(
            session->candidate_path,
            MORPH_AGENT_CANDIDATE_MAX_BYTES,
            1,
            "candidate.c",
            error,
            error_capacity) ||
        !morph_agent_hash_run_tree(
            session,
            session->provider_baseline,
            error,
            error_capacity)) {
        close(log_descriptor);
        return 0;
    }
    session->provider_baseline_valid = 1;
    arguments[0] = session->provider_path;
    arguments[1] = session->run_directory;
    arguments[2] = session->prompt_path;
    arguments[3] = session->diagnostics_path;
    arguments[4] = session->response_path;
    arguments[5] = NULL;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, log_descriptor, STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, log_descriptor, STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, log_descriptor);
    if (posix_spawnattr_init(&attributes) == 0) {
        short flags = POSIX_SPAWN_SETPGROUP;
        if (posix_spawnattr_setflags(&attributes, flags) == 0 &&
            posix_spawnattr_setpgroup(&attributes, 0) == 0) {
            attributes_ready = 1;
        } else {
            posix_spawnattr_destroy(&attributes);
        }
    }
    if (!attributes_ready) {
        posix_spawn_file_actions_destroy(&actions);
        close(log_descriptor);
        return morph_agent_error(
            error, error_capacity, "Unable to isolate agent provider process group");
    }
    spawn_result = posix_spawn(
        &session->process_id,
        session->provider_path,
        &actions,
        &attributes,
        arguments,
        environ);
    posix_spawnattr_destroy(&attributes);
    posix_spawn_file_actions_destroy(&actions);
    close(log_descriptor);
    if (spawn_result != 0) {
        session->process_id = 0;
        return morph_agent_error(error, error_capacity, strerror(spawn_result));
    }
    session->status = MORPH_AGENT_RUNNING;
    return 1;
}

int morph_agent_session_poll(
    morph_agent_session *session,
    int *finished,
    char *error,
    unsigned long error_capacity)
{
    int process_status;
    pid_t result;
    char attempt_candidate[MORPH_AGENT_PATH_CAPACITY];
    char attempt_directory[MORPH_AGENT_PATH_CAPACITY];
    char validation_error[MORPH_AGENT_FAILURE_CAPACITY] = {0};
    int provider_succeeded;

    if (finished) *finished = 0;
    if (session->status != MORPH_AGENT_RUNNING || session->process_id <= 0) {
        return morph_agent_error(error, error_capacity, "Agent provider is not running");
    }
    result = waitpid(session->process_id, &process_status, WNOHANG);
    if (result == 0) return 1;
    if (result < 0) {
        session->process_id = 0;
        session->status = MORPH_AGENT_PROVIDER_FAILED;
        return morph_agent_error(error, error_capacity, strerror(errno));
    }
    session->process_id = 0;
    (void)kill(-result, SIGKILL);
    provider_succeeded = WIFEXITED(process_status) && WEXITSTATUS(process_status) == 0;
    session->status = provider_succeeded
        ? MORPH_AGENT_PROVIDER_SUCCEEDED : MORPH_AGENT_PROVIDER_FAILED;
    if (!morph_agent_validate_provider_exit(
            session,
            provider_succeeded,
            validation_error,
            sizeof(validation_error))) {
        session->status = MORPH_AGENT_PROVIDER_FAILED;
        snprintf(
            session->failure_reason,
            sizeof(session->failure_reason),
            "%s",
            validation_error);
        (void)morph_agent_error(error, error_capacity, validation_error);
        if (finished) *finished = 1;
        return 1;
    }
    if (!morph_agent_path(
            attempt_directory,
            sizeof(attempt_directory),
            "%s/attempts/%02lu",
            session->run_directory,
            session->attempt) ||
        !morph_agent_path(
            attempt_candidate,
            sizeof(attempt_candidate),
            "%s/candidate.c",
            attempt_directory,
            0) ||
        !morph_agent_copy(
            session->candidate_path,
            attempt_candidate,
            validation_error,
            sizeof(validation_error))) {
        session->status = MORPH_AGENT_PROVIDER_FAILED;
        snprintf(
            session->failure_reason,
            sizeof(session->failure_reason),
            "Unable to retain validated agent candidate: %s",
            validation_error[0] ? validation_error : "artifact path is too long");
        (void)morph_agent_error(error, error_capacity, session->failure_reason);
    }
    if (finished) *finished = 1;
    return 1;
}

int morph_agent_session_record_build(
    const morph_agent_session *session,
    int succeeded,
    const char *stage,
    const char *diagnostics,
    char *error,
    unsigned long error_capacity)
{
    char path[MORPH_AGENT_PATH_CAPACITY];
    char header[256];
    FILE *file;
    int header_size;
    int failed;

    if (!morph_agent_path(
            path,
            sizeof(path),
            "%s/attempts/%02lu/build.txt",
            session->run_directory,
            session->attempt)) {
        return morph_agent_error(error, error_capacity, "Build artifact path is too long");
    }
    header_size = snprintf(
        header,
        sizeof(header),
        "success=%d\nstage=%s\n\n",
        succeeded ? 1 : 0,
        stage ? stage : "unknown");
    if (header_size < 0 || (unsigned long)header_size >= sizeof(header)) {
        return morph_agent_error(error, error_capacity, "Build artifact header is too large");
    }
    file = fopen(path, "wb");
    if (!file) return morph_agent_error(error, error_capacity, strerror(errno));
    failed = fwrite(header, 1, (size_t)header_size, file) != (size_t)header_size;
    if (!failed && diagnostics) {
        size_t size = strlen(diagnostics);
        failed = fwrite(diagnostics, 1, size, file) != size;
    }
    failed = fclose(file) != 0 || failed;
    return failed
        ? morph_agent_error(error, error_capacity, "Unable to record build artifact")
        : 1;
}

int morph_agent_session_create_patch(
    const morph_agent_session *session,
    char *error,
    unsigned long error_capacity)
{
    char patch_path[MORPH_AGENT_PATH_CAPACITY];
    char *arguments[5];
    posix_spawn_file_actions_t actions;
    pid_t process;
    int descriptor;
    int status;
    int spawn_result;

    if (!morph_agent_path(
            patch_path,
            sizeof(patch_path),
            "%s/patch.diff",
            session->run_directory,
            0)) {
        return morph_agent_error(error, error_capacity, "Patch path is too long");
    }
    descriptor = open(patch_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (descriptor < 0) return morph_agent_error(error, error_capacity, strerror(errno));
    arguments[0] = "/usr/bin/diff";
    arguments[1] = "-u";
    arguments[2] = (char *)session->source_before_path;
    arguments[3] = (char *)session->candidate_path;
    arguments[4] = NULL;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, descriptor, STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, descriptor);
    spawn_result = posix_spawn(&process, arguments[0], &actions, NULL, arguments, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(descriptor);
    if (spawn_result != 0) return morph_agent_error(error, error_capacity, strerror(spawn_result));
    if (waitpid(process, &status, 0) < 0 ||
        !WIFEXITED(status) ||
        (WEXITSTATUS(status) != 0 && WEXITSTATUS(status) != 1)) {
        return morph_agent_error(error, error_capacity, "Unable to create source patch");
    }
    return 1;
}

int morph_agent_session_candidate_changed(
    const morph_agent_session *session,
    char *error,
    unsigned long error_capacity)
{
    FILE *before = fopen(session->source_before_path, "rb");
    FILE *candidate;
    unsigned char before_buffer[16384];
    unsigned char candidate_buffer[16384];
    size_t before_count;
    size_t candidate_count;

    if (!before) return morph_agent_error(error, error_capacity, strerror(errno));
    candidate = fopen(session->candidate_path, "rb");
    if (!candidate) {
        fclose(before);
        return morph_agent_error(error, error_capacity, strerror(errno));
    }
    do {
        before_count = fread(before_buffer, 1, sizeof(before_buffer), before);
        candidate_count = fread(candidate_buffer, 1, sizeof(candidate_buffer), candidate);
        if (ferror(before) || ferror(candidate)) {
            fclose(before);
            fclose(candidate);
            return morph_agent_error(error, error_capacity, "Unable to compare agent candidate");
        }
        if (before_count != candidate_count ||
            (before_count && memcmp(before_buffer, candidate_buffer, before_count) != 0)) {
            fclose(before);
            fclose(candidate);
            return 1;
        }
    } while (before_count != 0);
    fclose(before);
    fclose(candidate);
    return morph_agent_error(error, error_capacity, "Agent returned without changing candidate.c");
}

int morph_agent_session_accept_source(
    const morph_agent_session *session,
    const char *destination_path,
    char *error,
    unsigned long error_capacity)
{
    char temporary[MORPH_AGENT_PATH_CAPACITY];
    int length = snprintf(temporary, sizeof(temporary), "%s.agent.tmp", destination_path);
    if (length < 0 || (unsigned long)length >= sizeof(temporary)) {
        return morph_agent_error(error, error_capacity, "Accepted source path is too long");
    }
    if (!morph_agent_copy(session->candidate_path, temporary, error, error_capacity)) return 0;
    if (rename(temporary, destination_path) != 0) {
        remove(temporary);
        return morph_agent_error(error, error_capacity, strerror(errno));
    }
    return 1;
}

int morph_agent_session_restore_source(
    const morph_agent_session *session,
    const char *destination_path,
    char *error,
    unsigned long error_capacity)
{
    char temporary[MORPH_AGENT_PATH_CAPACITY];
    int length = snprintf(temporary, sizeof(temporary), "%s.agent.tmp", destination_path);
    if (length < 0 || (unsigned long)length >= sizeof(temporary)) {
        return morph_agent_error(error, error_capacity, "Restored source path is too long");
    }
    if (!morph_agent_copy(session->source_before_path, temporary, error, error_capacity)) return 0;
    if (rename(temporary, destination_path) != 0) {
        remove(temporary);
        return morph_agent_error(error, error_capacity, strerror(errno));
    }
    return 1;
}

int morph_agent_session_record_outcome(
    const morph_agent_session *session,
    const char *outcome,
    unsigned long revision,
    char *error,
    unsigned long error_capacity)
{
    char path[MORPH_AGENT_PATH_CAPACITY];
    char document[256];
    int length;
    if (!morph_agent_path(path, sizeof(path), "%s/outcome.json", session->run_directory, 0)) {
        return morph_agent_error(error, error_capacity, "Outcome path is too long");
    }
    length = snprintf(
        document,
        sizeof(document),
        "{\n  \"outcome\": \"%s\",\n  \"revision\": %lu,\n  \"attempts\": %u\n}\n",
        outcome,
        revision,
        session->attempt);
    if (length < 0 || (unsigned long)length >= sizeof(document)) {
        return morph_agent_error(error, error_capacity, "Outcome artifact is too large");
    }
    return morph_agent_write(path, document, (unsigned long)length, error, error_capacity);
}

int morph_agent_session_read_provider_log(
    const morph_agent_session *session,
    char *output,
    unsigned long output_capacity)
{
    FILE *file;
    size_t size;
    if (!output || output_capacity == 0) return 0;
    output[0] = '\0';
    if (session->failure_reason[0]) {
        snprintf(output, (size_t)output_capacity, "%s", session->failure_reason);
        return 1;
    }
    file = fopen(session->provider_log_path, "rb");
    if (!file) return 0;
    size = fread(output, 1, (size_t)output_capacity - 1, file);
    fclose(file);
    output[size] = '\0';
    return size != 0;
}

void morph_agent_session_cancel(morph_agent_session *session)
{
    if (session->status == MORPH_AGENT_RUNNING && session->process_id > 0) {
        int status;
        kill(-session->process_id, SIGTERM);
        (void)waitpid(session->process_id, &status, 0);
        (void)kill(-session->process_id, SIGKILL);
    }
    session->process_id = 0;
    if (session->status == MORPH_AGENT_RUNNING) {
        session->status = MORPH_AGENT_PROVIDER_FAILED;
    }
}
