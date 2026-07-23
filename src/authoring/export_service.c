#include "export_service.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

#ifndef MORPHEUS_CMAKE_COMMAND
#define MORPHEUS_CMAKE_COMMAND "cmake"
#endif

#ifndef MORPHEUS_EXPORT_OUTPUT_DIR
#define MORPHEUS_EXPORT_OUTPUT_DIR "."
#endif

static int export_error(
    char *error,
    unsigned long error_capacity,
    const char *message)
{
    if (error && error_capacity) {
        snprintf(error, (size_t)error_capacity, "%s", message);
    }
    return 0;
}

void morph_export_service_init(
    morph_export_service *service,
    const char *tool_path)
{
    if (!service) return;
    memset(service, 0, sizeof(*service));
    if (tool_path) {
        snprintf(service->tool_path, sizeof(service->tool_path), "%s", tool_path);
    }
}

void morph_export_service_cancel(morph_export_service *service)
{
    int status;
    if (!service || service->status != MORPHEUS_AUTHORING_EXPORT_RUNNING ||
        service->process_id <= 0) return;
    (void)kill(service->process_id, SIGTERM);
    while (waitpid(service->process_id, &status, 0) < 0 && errno == EINTR) {}
    service->process_id = 0;
    service->status = MORPHEUS_AUTHORING_EXPORT_FAILED;
}

void morph_export_service_reset(morph_export_service *service)
{
    if (!service) return;
    morph_export_service_cancel(service);
    if (service->log_path[0]) unlink(service->log_path);
    service->output_path[0] = '\0';
    service->log_path[0] = '\0';
    service->process_id = 0;
    service->status = MORPHEUS_AUTHORING_EXPORT_IDLE;
}

int morph_export_service_start(
    morph_export_service *service,
    const char *source_path,
    const char *output_path,
    const char *application_name,
    const char *bundle_identifier,
    const char *application_version,
    char *error,
    unsigned long error_capacity)
{
    char log_template[] = "/private/tmp/morpheus-export-XXXXXX";
    char name_environment[512];
    char bundle_environment[512];
    char version_environment[512];
    char cmake_environment[MORPHEUS_AUTHORING_EXPORT_PATH_CAPACITY + 32];
    char *arguments[9];
    posix_spawn_file_actions_t actions;
    int descriptor;
    int spawn_result;
    int length;

    if (error && error_capacity) error[0] = '\0';
    if (!service || !source_path || !*source_path ||
        !output_path || !*output_path ||
        !application_name || !*application_name ||
        !bundle_identifier || !*bundle_identifier ||
        !application_version || !*application_version) {
        return export_error(error, error_capacity, "Export arguments are incomplete");
    }
    if (!service->tool_path[0] || access(service->tool_path, X_OK) != 0) {
        return export_error(error, error_capacity, "Export tool is not executable");
    }
    if (access(source_path, R_OK) != 0) {
        return export_error(error, error_capacity, "Accepted source is not readable");
    }

    morph_export_service_reset(service);
    length = output_path[0] == '/'
        ? snprintf(service->output_path, sizeof(service->output_path),
            "%s", output_path)
        : snprintf(service->output_path, sizeof(service->output_path),
            "%s/%s", MORPHEUS_EXPORT_OUTPUT_DIR, output_path);
    if (length < 0 || (unsigned long)length >= sizeof(service->output_path) ||
        snprintf(name_environment, sizeof(name_environment),
            "MORPHEUS_EXPORT_NAME=%s", application_name) >=
            (int)sizeof(name_environment) ||
        snprintf(bundle_environment, sizeof(bundle_environment),
            "MORPHEUS_EXPORT_BUNDLE_ID=%s", bundle_identifier) >=
            (int)sizeof(bundle_environment) ||
        snprintf(version_environment, sizeof(version_environment),
            "MORPHEUS_EXPORT_VERSION=%s", application_version) >=
            (int)sizeof(version_environment) ||
        snprintf(cmake_environment, sizeof(cmake_environment),
            "MORPHEUS_CMAKE_COMMAND=%s", MORPHEUS_CMAKE_COMMAND) >=
            (int)sizeof(cmake_environment)) {
        return export_error(error, error_capacity, "Export metadata is too long");
    }

    descriptor = mkstemp(log_template);
    if (descriptor < 0) {
        return export_error(error, error_capacity, strerror(errno));
    }
    snprintf(service->log_path, sizeof(service->log_path), "%s", log_template);

    arguments[0] = "/usr/bin/env";
    arguments[1] = name_environment;
    arguments[2] = bundle_environment;
    arguments[3] = version_environment;
    arguments[4] = cmake_environment;
    arguments[5] = service->tool_path;
    arguments[6] = service->output_path;
    arguments[7] = (char *)source_path;
    arguments[8] = NULL;

    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, descriptor, STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, descriptor, STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, descriptor);
    spawn_result = posix_spawn(
        &service->process_id,
        arguments[0],
        &actions,
        NULL,
        arguments,
        environ);
    posix_spawn_file_actions_destroy(&actions);
    close(descriptor);
    if (spawn_result != 0) {
        unlink(service->log_path);
        service->log_path[0] = '\0';
        service->output_path[0] = '\0';
        service->process_id = 0;
        return export_error(error, error_capacity, strerror(spawn_result));
    }
    service->status = MORPHEUS_AUTHORING_EXPORT_RUNNING;
    return 1;
}

int morph_export_service_poll(
    morph_export_service *service,
    int *finished,
    char *error,
    unsigned long error_capacity)
{
    int process_status;
    pid_t result;
    if (error && error_capacity) error[0] = '\0';
    if (!service || !finished) {
        return export_error(error, error_capacity, "Export poll arguments are invalid");
    }
    *finished = service->status != MORPHEUS_AUTHORING_EXPORT_RUNNING;
    if (*finished) return 1;

    result = waitpid(service->process_id, &process_status, WNOHANG);
    if (result == 0) return 1;
    if (result < 0) {
        if (errno == EINTR) return 1;
        service->process_id = 0;
        service->status = MORPHEUS_AUTHORING_EXPORT_FAILED;
        *finished = 1;
        return export_error(error, error_capacity, strerror(errno));
    }
    service->process_id = 0;
    *finished = 1;
    if (WIFEXITED(process_status) && WEXITSTATUS(process_status) == 0) {
        service->status = MORPHEUS_AUTHORING_EXPORT_SUCCEEDED;
        return 1;
    }
    service->status = MORPHEUS_AUTHORING_EXPORT_FAILED;
    if (WIFEXITED(process_status)) {
        char message[128];
        snprintf(message, sizeof(message),
            "Export tool exited with status %d", WEXITSTATUS(process_status));
        return export_error(error, error_capacity, message);
    }
    return export_error(error, error_capacity, "Export tool terminated unexpectedly");
}

int morph_export_service_read_log(
    const morph_export_service *service,
    char *output,
    unsigned long output_capacity)
{
    FILE *file;
    long end;
    long offset = 0;
    size_t size;
    if (!output || output_capacity == 0) return 0;
    output[0] = '\0';
    if (!service || !service->log_path[0]) return 0;
    file = fopen(service->log_path, "rb");
    if (!file) return 0;
    if (fseek(file, 0, SEEK_END) == 0 && (end = ftell(file)) > 0 &&
        end >= (long)output_capacity) {
        offset = end - (long)output_capacity + 1;
    }
    if (fseek(file, offset, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }
    size = fread(output, 1, (size_t)output_capacity - 1, file);
    output[size] = '\0';
    fclose(file);
    return 1;
}
