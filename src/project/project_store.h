#ifndef MORPHEUS_PROJECT_STORE_H
#define MORPHEUS_PROJECT_STORE_H

#define MORPH_PROJECT_MAX_PROJECTS 32
#define MORPH_PROJECT_NAME_CAPACITY 64
#define MORPH_PROJECT_SLUG_CAPACITY 64
#define MORPH_PROJECT_PATH_CAPACITY 4096

typedef struct morph_project_entry {
    char name[MORPH_PROJECT_NAME_CAPACITY];
    char slug[MORPH_PROJECT_SLUG_CAPACITY];
} morph_project_entry;

typedef struct morph_project_store {
    char root[MORPH_PROJECT_PATH_CAPACITY];
    morph_project_entry projects[MORPH_PROJECT_MAX_PROJECTS];
    unsigned int count;
    unsigned int active_index;
} morph_project_store;

int morph_project_store_init(
    morph_project_store *store,
    const char *root,
    char *error,
    unsigned long error_capacity);
int morph_project_store_create(
    morph_project_store *store,
    const char *name,
    char *error,
    unsigned long error_capacity);
int morph_project_store_select(
    morph_project_store *store,
    unsigned int index,
    char *error,
    unsigned long error_capacity);
int morph_project_store_paths(
    const morph_project_store *store,
    char *workspace,
    unsigned long workspace_capacity,
    char *source,
    unsigned long source_capacity,
    char *assets,
    unsigned long assets_capacity);

#endif
