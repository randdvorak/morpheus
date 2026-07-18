#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "agent_session.h"

static int write_file(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    size_t size = strlen(text);
    int failed;
    if (!file) return 0;
    failed = fwrite(text, 1, size, file) != size;
    failed = fclose(file) != 0 || failed;
    return !failed;
}

static int file_contains(const char *path, const char *needle)
{
    char buffer[8192];
    FILE *file = fopen(path, "rb");
    size_t size;
    if (!file) return 0;
    size = fread(buffer, 1, sizeof(buffer) - 1, file);
    fclose(file);
    buffer[size] = '\0';
    return strstr(buffer, needle) != NULL;
}

static void cleanup(const morph_agent_session *session, const char *root)
{
    const char *attempt_files[] = {
        "diagnostics.txt", "prompt.txt", "response.txt", "provider.log", "candidate.c", "build.txt"
    };
    const char *run_files[] = {
        "candidate.c", "source-before.c", "request.txt", "app_api.h", "patch.diff", "outcome.json"
    };
    char path[MORPH_AGENT_PATH_CAPACITY];
    size_t index;

    for (index = 0; index < sizeof(attempt_files) / sizeof(attempt_files[0]); ++index) {
        snprintf(path, sizeof(path), "%s/attempts/01/%s", session->run_directory, attempt_files[index]);
        unlink(path);
    }
    snprintf(path, sizeof(path), "%s/attempts/01", session->run_directory);
    rmdir(path);
    snprintf(path, sizeof(path), "%s/attempts", session->run_directory);
    rmdir(path);
    for (index = 0; index < sizeof(run_files) / sizeof(run_files[0]); ++index) {
        snprintf(path, sizeof(path), "%s/%s", session->run_directory, run_files[index]);
        unlink(path);
    }
    rmdir(session->run_directory);
    snprintf(path, sizeof(path), "%s/runs", root);
    rmdir(path);
    snprintf(path, sizeof(path), "%s/source.c", root);
    unlink(path);
    snprintf(path, sizeof(path), "%s/api.h", root);
    unlink(path);
    snprintf(path, sizeof(path), "%s/accepted.c", root);
    unlink(path);
    rmdir(root);
}

int main(void)
{
    char root[] = "/private/tmp/morpheus-agent-XXXXXX";
    char source_path[MORPH_AGENT_PATH_CAPACITY];
    char api_path[MORPH_AGENT_PATH_CAPACITY];
    char accepted_path[MORPH_AGENT_PATH_CAPACITY];
    char artifact_path[MORPH_AGENT_PATH_CAPACITY];
    char error[1024] = {0};
    morph_agent_session session;
    struct timespec delay = {0, 1000000};
    int finished = 0;
    int polls = 0;

    if (!mkdtemp(root)) {
        perror("mkdtemp");
        return 1;
    }
    morph_agent_session_reset(&session);
    snprintf(source_path, sizeof(source_path), "%s/source.c", root);
    snprintf(api_path, sizeof(api_path), "%s/api.h", root);
    snprintf(accepted_path, sizeof(accepted_path), "%s/accepted.c", root);
    if (!write_file(source_path, "int original;\n") ||
        !write_file(api_path, "/* api */\n") ||
        !morph_agent_session_init(
            &session,
            root,
            MORPHEUS_FAKE_AGENT_PATH,
            error,
            sizeof(error)) ||
        !morph_agent_session_begin(
            &session,
            "Add a visible label",
            source_path,
            api_path,
            error,
            sizeof(error)) ||
        !morph_agent_session_start_attempt(
            &session,
            "first diagnostic",
            error,
            sizeof(error))) {
        fprintf(stderr, "agent setup failed: %s\n", error);
        cleanup(&session, root);
        return 2;
    }

    while (!finished && polls++ < 500) {
        if (!morph_agent_session_poll(&session, &finished, error, sizeof(error))) {
            fprintf(stderr, "agent poll failed: %s\n", error);
            cleanup(&session, root);
            return 3;
        }
        if (!finished) nanosleep(&delay, NULL);
    }
    if (!finished || session.status != MORPH_AGENT_PROVIDER_SUCCEEDED ||
        !file_contains(session.candidate_path, "fake external agent edit") ||
        !file_contains(session.response_path, "candidate updated") ||
        !file_contains(session.prompt_path, "Add a visible label") ||
        !file_contains(session.prompt_path, "first diagnostic")) {
        fprintf(stderr, "external provider did not produce expected artifacts\n");
        cleanup(&session, root);
        return 4;
    }

    if (!morph_agent_session_record_build(
            &session,
            1,
            "active",
            "build passed",
            error,
            sizeof(error)) ||
        !morph_agent_session_create_patch(&session, error, sizeof(error)) ||
        !morph_agent_session_accept_source(
            &session,
            accepted_path,
            error,
            sizeof(error)) ||
        !morph_agent_session_record_outcome(
            &session,
            "accepted",
            7,
            error,
            sizeof(error))) {
        fprintf(stderr, "artifact recording failed: %s\n", error);
        cleanup(&session, root);
        return 5;
    }
    snprintf(artifact_path, sizeof(artifact_path), "%s/patch.diff", session.run_directory);
    if (!file_contains(artifact_path, "fake external agent edit") ||
        !file_contains(accepted_path, "fake external agent edit")) {
        fprintf(stderr, "patch or accepted source was not recorded\n");
        cleanup(&session, root);
        return 6;
    }
    if (!morph_agent_session_restore_source(
            &session,
            accepted_path,
            error,
            sizeof(error)) ||
        !file_contains(accepted_path, "int original;")) {
        fprintf(stderr, "source rollback was not recorded\n");
        cleanup(&session, root);
        return 7;
    }
    snprintf(artifact_path, sizeof(artifact_path), "%s/outcome.json", session.run_directory);
    if (!file_contains(artifact_path, "\"revision\": 7")) {
        fprintf(stderr, "accepted outcome was not recorded\n");
        cleanup(&session, root);
        return 8;
    }

    cleanup(&session, root);
    puts("PASS: external provider protocol and durable agent artifacts");
    return 0;
}
