#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TEST_CANDIDATE_MAX_BYTES (1024UL * 1024UL)

static int path_join(char *output, size_t capacity, const char *root, const char *name)
{
    int length = snprintf(output, capacity, "%s/%s", root, name);
    return length >= 0 && (size_t)length < capacity;
}

static int append_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "ab");
    int failed;
    if (!file) return 0;
    failed = fputs(text, file) < 0;
    failed = fclose(file) != 0 || failed;
    return !failed;
}

int main(int argc, char **argv)
{
    const char *action = getenv("MORPHEUS_FAKE_BOUNDARY_ACTION");
    char path[4096];
    FILE *file;

    if (argc != 5 || !action) return 2;
    if (strcmp(action, "protected") == 0) {
        if (!path_join(path, sizeof(path), argv[1], "app_api.h") ||
            !append_text(path, "/* unauthorized change */\n")) return 3;
    } else if (strcmp(action, "unexpected") == 0) {
        if (!path_join(path, sizeof(path), argv[1], "unexpected.txt")) return 4;
        file = fopen(path, "wb");
        if (!file || fputs("unexpected\n", file) < 0 || fclose(file) != 0) return 5;
    } else if (strcmp(action, "symlink") == 0) {
        if (!path_join(path, sizeof(path), argv[1], "escape") ||
            symlink("/private/tmp", path) != 0) return 6;
    } else if (strcmp(action, "candidate-symlink") == 0) {
        char source_before[4096];
        if (!path_join(path, sizeof(path), argv[1], "candidate.c") ||
            !path_join(source_before, sizeof(source_before), argv[1], "source-before.c") ||
            unlink(path) != 0 || symlink(source_before, path) != 0) return 7;
    } else if (strcmp(action, "oversized") == 0) {
        if (!path_join(path, sizeof(path), argv[1], "candidate.c")) return 8;
        file = fopen(path, "ab");
        if (!file || ftruncate(fileno(file), (off_t)TEST_CANDIDATE_MAX_BYTES + 1) != 0 ||
            fclose(file) != 0) return 9;
    } else {
        return 10;
    }

    file = fopen(argv[4], "wb");
    if (!file) return 11;
    if (fputs("provider completed\n", file) < 0 || fclose(file) != 0) return 12;
    (void)argv[2];
    (void)argv[3];
    return 0;
}
