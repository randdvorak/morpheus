#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    char candidate_path[4096];
    FILE *candidate;
    FILE *response;
    int length;

    if (argc != 5) return 2;
    length = snprintf(candidate_path, sizeof(candidate_path), "%s/candidate.c", argv[1]);
    if (length < 0 || (size_t)length >= sizeof(candidate_path)) return 3;
    candidate = fopen(candidate_path, "ab");
    if (!candidate) return 4;
    if (fputs("\n/* fake external agent edit */\n", candidate) < 0) {
        fclose(candidate);
        return 5;
    }
    if (fclose(candidate) != 0) return 5;
    response = fopen(argv[4], "wb");
    if (!response) return 6;
    if (fputs("candidate updated\n", response) < 0) {
        fclose(response);
        return 7;
    }
    if (fclose(response) != 0) return 7;
    (void)argv[2];
    (void)argv[3];
    return 0;
}
