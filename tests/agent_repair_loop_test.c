#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "agent_session.h"
#include "runtime_module.h"

static void host_log(morph_host *host, const char *message)
{
    (void)host;
    (void)message;
}

static void host_label(morph_host *host, const char *text)
{
    (void)host;
    (void)text;
}

static int host_button(morph_host *host, const char *text)
{
    (void)host;
    (void)text;
    return 0;
}

static int wait_for_provider(
    morph_agent_session *session,
    char *error,
    unsigned long error_capacity)
{
    struct timespec delay = {0, 1000000};
    int finished = 0;
    int polls = 0;
    while (!finished && polls++ < 500) {
        if (!morph_agent_session_poll(session, &finished, error, error_capacity)) return 0;
        if (!finished) nanosleep(&delay, NULL);
    }
    return finished && session->status == MORPH_AGENT_PROVIDER_SUCCEEDED;
}

static void cleanup(const morph_agent_session *session, const char *root)
{
    const char *attempt_files[] = {
        "diagnostics.txt", "prompt.txt", "response.txt", "provider.log", "candidate.c", "build.txt"
    };
    const char *run_files[] = {
        "candidate.c", "source-before.c", "request.txt", "app_api.h", "model.txt", "patch.diff", "outcome.json"
    };
    char path[MORPH_AGENT_PATH_CAPACITY];
    unsigned int attempt;
    size_t index;
    for (attempt = 1; attempt <= session->attempt; ++attempt) {
        for (index = 0; index < sizeof(attempt_files) / sizeof(attempt_files[0]); ++index) {
            snprintf(path, sizeof(path), "%s/attempts/%02u/%s", session->run_directory, attempt, attempt_files[index]);
            unlink(path);
        }
        snprintf(path, sizeof(path), "%s/attempts/%02u", session->run_directory, attempt);
        rmdir(path);
    }
    snprintf(path, sizeof(path), "%s/attempts", session->run_directory);
    rmdir(path);
    for (index = 0; index < sizeof(run_files) / sizeof(run_files[0]); ++index) {
        snprintf(path, sizeof(path), "%s/%s", session->run_directory, run_files[index]);
        unlink(path);
    }
    rmdir(session->run_directory);
    snprintf(path, sizeof(path), "%s/runs", root);
    rmdir(path);
    rmdir(root);
}

int main(void)
{
    char root[] = "/private/tmp/morpheus-repair-XXXXXX";
    char error[4096] = {0};
    morph_agent_session session;
    morph_runtime_module module;
    morph_host host = {
        MORPHEUS_HOST_ABI_VERSION,
        NULL,
        host_log,
        host_label,
        host_button,
        NULL,
        NULL
    };

    if (!mkdtemp(root)) {
        perror("mkdtemp");
        return 1;
    }
    morph_runtime_module_init(&module);
    if (!morph_agent_session_init(
            &session,
            root,
            MORPHEUS_REPAIR_AGENT_PATH,
            error,
            sizeof(error)) ||
        !morph_agent_session_begin(
            &session,
            "produce a valid visible change",
            MORPHEUS_VALID_MODULE,
            MORPHEUS_API_HEADER,
            error,
            sizeof(error)) ||
        !morph_agent_session_start_attempt(&session, "", error, sizeof(error)) ||
        !wait_for_provider(&session, error, sizeof(error))) {
        fprintf(stderr, "first provider attempt failed: %s\n", error);
        cleanup(&session, root);
        return 2;
    }
    if (morph_runtime_module_compile_candidate(
            &module,
            session.candidate_path,
            error,
            sizeof(error)) ||
        module.last_stage != MORPH_RUNTIME_STAGE_COMPILE) {
        fprintf(stderr, "first candidate unexpectedly compiled\n");
        cleanup(&session, root);
        return 3;
    }
    if (!morph_agent_session_record_build(
            &session,
            0,
            morph_runtime_stage_name(module.last_stage),
            error,
            error,
            sizeof(error)) ||
        !morph_agent_session_start_attempt(&session, error, error, sizeof(error)) ||
        !wait_for_provider(&session, error, sizeof(error))) {
        fprintf(stderr, "diagnostic repair attempt failed: %s\n", error);
        cleanup(&session, root);
        return 4;
    }
    if (!morph_runtime_module_compile_candidate(
            &module,
            session.candidate_path,
            error,
            sizeof(error)) ||
        !morph_runtime_module_activate_candidate(
            &module,
            &host,
            error,
            sizeof(error)) ||
        !module.api || strcmp(module.api->name, "version-one") != 0) {
        fprintf(stderr, "repaired candidate did not activate: %s\n", error);
        morph_runtime_module_destroy(&module, &host);
        cleanup(&session, root);
        return 5;
    }

    morph_runtime_module_destroy(&module, &host);
    cleanup(&session, root);
    puts("PASS: compiler diagnostics drove an external-agent repair attempt");
    return 0;
}
