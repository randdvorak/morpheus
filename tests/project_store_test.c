#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "project_store.h"

int main(void)
{
    char root[] = "/private/tmp/morpheus-projects-XXXXXX";
    char workspace[MORPH_PROJECT_PATH_CAPACITY];
    char source[MORPH_PROJECT_PATH_CAPACITY];
    char assets[MORPH_PROJECT_PATH_CAPACITY];
    char error[256] = {0};
    morph_project_store store;
    morph_project_store reopened;

    if (!mkdtemp(root) ||
        !morph_project_store_init(&store, root, error, sizeof(error)) ||
        store.count != 1 || strcmp(store.projects[0].slug, "my-app") != 0 ||
        !morph_project_store_create(&store, "Second Project", error, sizeof(error)) ||
        store.count != 2 || strcmp(store.projects[store.active_index].slug, "second-project") != 0 ||
        !morph_project_store_paths(&store,
            workspace, sizeof(workspace), source, sizeof(source), assets, sizeof(assets)) ||
        access(source, R_OK) != 0 || access(assets, F_OK) != 0 ||
        !morph_project_store_init(&reopened, root, error, sizeof(error)) ||
        strcmp(reopened.projects[reopened.active_index].name, "Second Project") != 0) {
        fprintf(stderr, "project store test failed: %s\n", error);
        return 1;
    }
    puts("PASS: project creation, discovery, paths, and active selection");
    return 0;
}
