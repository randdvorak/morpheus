#define _POSIX_C_SOURCE 200809L

#include "agent_session.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

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
    char *arguments[6];
    int log_descriptor;
    int spawn_result;

    if (session->status == MORPH_AGENT_RUNNING ||
        session->attempt >= MORPH_AGENT_MAX_ATTEMPTS) {
        return morph_agent_error(error, error_capacity, "Agent attempt cannot be started");
    }
    session->attempt += 1;
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
    spawn_result = posix_spawn(
        &session->process_id,
        session->provider_path,
        &actions,
        NULL,
        arguments,
        environ);
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
    session->status = WIFEXITED(process_status) && WEXITSTATUS(process_status) == 0
        ? MORPH_AGENT_PROVIDER_SUCCEEDED
        : MORPH_AGENT_PROVIDER_FAILED;
    if (morph_agent_path(
            attempt_directory,
            sizeof(attempt_directory),
            "%s/attempts/%02lu",
            session->run_directory,
            session->attempt) &&
        morph_agent_path(
            attempt_candidate,
            sizeof(attempt_candidate),
            "%s/candidate.c",
            attempt_directory,
            0)) {
        (void)morph_agent_copy(
            session->candidate_path,
            attempt_candidate,
            error,
            error_capacity);
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
        kill(session->process_id, SIGTERM);
        (void)waitpid(session->process_id, &status, 0);
    }
    session->process_id = 0;
    if (session->status == MORPH_AGENT_RUNNING) {
        session->status = MORPH_AGENT_PROVIDER_FAILED;
    }
}
