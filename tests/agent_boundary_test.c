#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "agent_session.h"

typedef struct boundary_case {
    const char *action;
    const char *expected_error;
} boundary_case;

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

static void remove_tree(const char *path)
{
    struct stat status;
    if (lstat(path, &status) != 0) return;
    if (S_ISDIR(status.st_mode)) {
        DIR *directory = opendir(path);
        struct dirent *entry;
        if (!directory) return;
        while ((entry = readdir(directory)) != NULL) {
            char child[MORPH_AGENT_PATH_CAPACITY];
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
            if (snprintf(child, sizeof(child), "%s/%s", path, entry->d_name) > 0) {
                remove_tree(child);
            }
        }
        closedir(directory);
        rmdir(path);
    } else {
        unlink(path);
    }
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
    return finished;
}

static int run_case(const boundary_case *test_case)
{
    char root[] = "/private/tmp/morpheus-boundary-XXXXXX";
    char source[MORPH_AGENT_PATH_CAPACITY];
    char api[MORPH_AGENT_PATH_CAPACITY];
    char sdk[MORPH_AGENT_PATH_CAPACITY];
    char error[1024] = {0};
    char failure[1024] = {0};
    morph_agent_session session;
    int succeeded = 0;

    if (!mkdtemp(root)) return 0;
    snprintf(source, sizeof(source), "%s/source.c", root);
    snprintf(api, sizeof(api), "%s/api.h", root);
    snprintf(sdk, sizeof(sdk), "%s/sdk.h", root);
    if (!write_file(source, "int original;\n") ||
        !write_file(api, "/* api */\n") ||
        !write_file(sdk, "/* sdk */\n") ||
        !morph_agent_session_init(
            &session, root, MORPHEUS_FAKE_BOUNDARY_AGENT_PATH, error, sizeof(error)) ||
        !morph_agent_session_begin(
            &session, "test the provider boundary", source, api, sdk, error, sizeof(error)) ||
        setenv("MORPHEUS_FAKE_BOUNDARY_ACTION", test_case->action, 1) != 0 ||
        !morph_agent_session_start_attempt(&session, "", error, sizeof(error))) {
        fprintf(stderr, "%s setup failed: %s\n", test_case->action, error);
        unsetenv("MORPHEUS_FAKE_BOUNDARY_ACTION");
        remove_tree(root);
        return 0;
    }
    unsetenv("MORPHEUS_FAKE_BOUNDARY_ACTION");
    if (wait_for_provider(&session, error, sizeof(error)) &&
        session.status == MORPH_AGENT_PROVIDER_FAILED &&
        morph_agent_session_read_provider_log(&session, failure, sizeof(failure)) &&
        strstr(failure, test_case->expected_error) != NULL) {
        succeeded = 1;
    } else {
        fprintf(
            stderr,
            "%s was not rejected as expected: poll=%s failure=%s\n",
            test_case->action,
            error,
            failure);
    }
    remove_tree(root);
    return succeeded;
}

int main(void)
{
    const boundary_case cases[] = {
        {"protected", "protected or unexpected artifacts changed"},
        {"unexpected", "protected or unexpected artifacts changed"},
        {"symlink", "symbolic link"},
        {"candidate-symlink", "candidate.c is not a regular file"},
        {"oversized", "candidate.c exceeds its size limit"}
    };
    size_t index;
    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        if (!run_case(&cases[index])) return (int)index + 1;
    }
    puts("PASS: provider artifact boundary rejects unsafe post-exit changes");
    return 0;
}
