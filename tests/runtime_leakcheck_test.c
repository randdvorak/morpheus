#include <stdio.h>

#include "runtime_leakcheck.h"

int main(void)
{
    void *first;
    void *second;

    first = morph_runtime_leakcheck_malloc(8, __FILE__, __LINE__);
    second = morph_runtime_leakcheck_calloc(4, 4, __FILE__, __LINE__);
    if (!first || !second ||
        morph_runtime_leakcheck_live_allocations() != 2 ||
        morph_runtime_leakcheck_live_bytes() != 24) {
        fprintf(stderr, "initial leak accounting failed\n");
        return 1;
    }

    first = morph_runtime_leakcheck_realloc(first, 24, __FILE__, __LINE__);
    if (!first ||
        morph_runtime_leakcheck_live_allocations() != 2 ||
        morph_runtime_leakcheck_live_bytes() != 40) {
        fprintf(stderr, "reallocation accounting failed\n");
        return 2;
    }

    morph_runtime_leakcheck_free(first);
    morph_runtime_leakcheck_free(second);
    if (morph_runtime_leakcheck_live_allocations() != 0 ||
        morph_runtime_leakcheck_live_bytes() != 0 ||
        morph_runtime_leakcheck_report() != 0) {
        fprintf(stderr, "free accounting failed\n");
        return 3;
    }

    first = morph_runtime_leakcheck_malloc(7, __FILE__, __LINE__);
    if (!first || morph_runtime_leakcheck_report() != 1) {
        fprintf(stderr, "leak reporting failed\n");
        return 4;
    }
    morph_runtime_leakcheck_free(first);

    puts("PASS: optional generated-module leak accounting and reporting");
    return 0;
}
