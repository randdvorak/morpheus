#include <stdio.h>

static int copy_file(const char *source_path, const char *destination_path)
{
    unsigned char buffer[4096];
    FILE *source = fopen(source_path, "rb");
    FILE *destination;
    size_t count;
    int failed = 0;
    if (!source) return 0;
    destination = fopen(destination_path, "wb");
    if (!destination) {
        fclose(source);
        return 0;
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
    return !failed;
}

int main(int argc, char **argv)
{
    char candidate_path[4096];
    FILE *diagnostics;
    FILE *response;
    int has_diagnostics;
    int length;
    int byte;

    if (argc != 5) return 2;
    diagnostics = fopen(argv[3], "rb");
    if (!diagnostics) return 3;
    byte = fgetc(diagnostics);
    has_diagnostics = byte != EOF;
    fclose(diagnostics);
    length = snprintf(candidate_path, sizeof(candidate_path), "%s/candidate.c", argv[1]);
    if (length < 0 || (size_t)length >= sizeof(candidate_path)) return 4;
    if (!copy_file(
            has_diagnostics ? MORPHEUS_VALID_MODULE : MORPHEUS_INVALID_MODULE,
            candidate_path)) return 5;
    response = fopen(argv[4], "wb");
    if (!response) return 6;
    if (fputs(has_diagnostics ? "repaired candidate\n" : "initial candidate\n", response) < 0 ||
        fclose(response) != 0) return 7;
    (void)argv[2];
    return 0;
}
