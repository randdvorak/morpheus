#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "authoring_capabilities.h"
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
        "candidate.c", "source-before.c", "request.txt", "app_api.h", "sdk.h", "model.txt", "patch.diff", "outcome.json"
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
    snprintf(path, sizeof(path), "%s/sdk.h", root);
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
    char sdk_path[MORPH_AGENT_PATH_CAPACITY];
    char accepted_path[MORPH_AGENT_PATH_CAPACITY];
    char artifact_path[MORPH_AGENT_PATH_CAPACITY];
    char error[1024] = {0};
    morph_agent_session session;
    morph_capability agent_capability;
    morph_capability_registry registry;
    morph_host authoring_host = {0};
    const morph_capability *provider;
    const morph_authoring_agent_api *agent;
    void *agent_context;
    struct timespec delay = {0, 1000000};
    int finished = 0;
    int polls = 0;

    if (!mkdtemp(root)) {
        perror("mkdtemp");
        return 1;
    }
    agent_capability = morph_authoring_agent_capability(&session);
    registry.entries = &agent_capability;
    registry.count = 1;
    authoring_host.abi_version = MORPHEUS_HOST_ABI_VERSION;
    authoring_host.capabilities = &registry;
    provider = morph_host_find_capability(
        &authoring_host,
        MORPHEUS_AUTHORING_AGENT_CAPABILITY,
        MORPHEUS_AUTHORING_AGENT_ABI_VERSION);
    agent = morph_authoring_agent_from_capability(provider);
    agent_context = provider ? provider->context : NULL;
    if (!agent) {
        fprintf(stderr, "agent capability discovery failed\n");
        rmdir(root);
        return 11;
    }
    agent->reset(agent_context);
    snprintf(source_path, sizeof(source_path), "%s/source.c", root);
    snprintf(api_path, sizeof(api_path), "%s/api.h", root);
    snprintf(sdk_path, sizeof(sdk_path), "%s/sdk.h", root);
    snprintf(accepted_path, sizeof(accepted_path), "%s/accepted.c", root);
    if (!write_file(source_path, "int original;\n") ||
        !write_file(api_path, "/* api */\n") ||
        !write_file(sdk_path, "/* sdk */\n") ||
        !agent->init(
            agent_context,
            root,
            MORPHEUS_FAKE_AGENT_PATH,
            error,
            sizeof(error)) ||
        !agent->set_model(
            agent_context,
            "test-model",
            error,
            sizeof(error)) ||
        !agent->begin(
            agent_context,
            "Add a visible label",
            source_path,
            api_path,
            sdk_path,
            error,
            sizeof(error)) ||
        !agent->start_attempt(
            agent_context,
            "first diagnostic",
            error,
            sizeof(error))) {
        fprintf(stderr, "agent setup failed: %s\n", error);
        cleanup(&session, root);
        return 2;
    }

    while (!finished && polls++ < 500) {
        if (!agent->poll(
                agent_context, &finished, error, sizeof(error))) {
            fprintf(stderr, "agent poll failed: %s\n", error);
            cleanup(&session, root);
            return 3;
        }
        if (!finished) nanosleep(&delay, NULL);
    }
    if (!finished || agent->status(agent_context) !=
            MORPHEUS_AUTHORING_AGENT_PROVIDER_SUCCEEDED ||
        !file_contains(agent->candidate_path(agent_context), "fake external agent edit") ||
        !file_contains(session.response_path, "candidate updated") ||
        !file_contains(session.prompt_path, "Add a visible label") ||
        !file_contains(session.prompt_path, "app_api.h and sdk.h") ||
        !file_contains(session.prompt_path, "morph_image_load_rgba") ||
        !file_contains(session.prompt_path, "first diagnostic")) {
        fprintf(stderr, "external provider did not produce expected artifacts\n");
        cleanup(&session, root);
        return 4;
    }

    if (!agent->candidate_changed(agent_context, error, sizeof(error))) {
        fprintf(stderr, "changed candidate was reported as unchanged: %s\n", error);
        cleanup(&session, root);
        return 5;
    }
    if (!write_file(agent->candidate_path(agent_context), "int original;\n") ||
        agent->candidate_changed(agent_context, error, sizeof(error)) ||
        !strstr(error, "without changing candidate.c") ||
        !write_file(
            agent->candidate_path(agent_context),
            "int original;\n\n/* fake external agent edit */\n")) {
        fprintf(stderr, "unchanged candidate was not rejected\n");
        cleanup(&session, root);
        return 6;
    }
    error[0] = '\0';

    if (!agent->record_build(
            agent_context,
            1,
            "active",
            "build passed",
            error,
            sizeof(error)) ||
        !agent->create_patch(agent_context, error, sizeof(error)) ||
        !agent->accept_source(
            agent_context,
            accepted_path,
            error,
            sizeof(error)) ||
        !agent->record_outcome(
            agent_context,
            "accepted",
            7,
            error,
            sizeof(error))) {
        fprintf(stderr, "artifact recording failed: %s\n", error);
        cleanup(&session, root);
        return 7;
    }
    snprintf(artifact_path, sizeof(artifact_path), "%s/patch.diff", session.run_directory);
    if (!file_contains(artifact_path, "fake external agent edit") ||
        !file_contains(accepted_path, "fake external agent edit")) {
        fprintf(stderr, "patch or accepted source was not recorded\n");
        cleanup(&session, root);
        return 8;
    }
    if (!agent->restore_source(
            agent_context,
            accepted_path,
            error,
            sizeof(error)) ||
        !file_contains(accepted_path, "int original;")) {
        fprintf(stderr, "source rollback was not recorded\n");
        cleanup(&session, root);
        return 9;
    }
    snprintf(artifact_path, sizeof(artifact_path), "%s/outcome.json", session.run_directory);
    if (!file_contains(artifact_path, "\"revision\": 7")) {
        fprintf(stderr, "accepted outcome was not recorded\n");
        cleanup(&session, root);
        return 10;
    }

    cleanup(&session, root);
    puts("PASS: external provider protocol and durable agent artifacts");
    return 0;
}
