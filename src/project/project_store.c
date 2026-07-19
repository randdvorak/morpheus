#include "project_store.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char starter_source[] =
    "#include \"morpheus/app_api.h\"\n"
    "#include <stdio.h>\n"
    "\n"
    "typedef struct starter_state { unsigned int clicks; } starter_state;\n"
    "static starter_state storage;\n"
    "static int create(morph_host *host, void **state) {\n"
    "    storage.clicks = 0; *state = &storage;\n"
    "    host->log(host, \"New Morpheus app ready\"); return 1;\n"
    "}\n"
    "static void destroy(morph_host *host, void *state) {(void)host;(void)state;}\n"
    "static void update(morph_host *host, void *state, double dt) {(void)host;(void)state;(void)dt;}\n"
    "static void render(morph_host *host, void *state) {\n"
    "    starter_state *app = state; char text[64];\n"
    "    snprintf(text, sizeof(text), \"Button clicks: %u\", app->clicks);\n"
    "    host->ui_label(host, \"Start building your new app.\");\n"
    "    host->ui_label(host, text);\n"
    "    if (host->ui_button(host, \"Click me\")) ++app->clicks;\n"
    "}\n"
    "static int save(morph_host *host, void *state, const void **data, unsigned long *size) {\n"
    "    (void)host; *data = state; *size = sizeof(starter_state); return 1;\n"
    "}\n"
    "static int load(morph_host *host, void **state, const void *data, unsigned long size) {\n"
    "    (void)host; if (size == sizeof(starter_state)) storage = *(const starter_state *)data;\n"
    "    *state = &storage; return 1;\n"
    "}\n"
    "static const morph_app_api api = {MORPHEUS_APP_ABI_VERSION, \"New App\", create, destroy, update, render, save, load};\n"
    "const morph_app_api *morph_app_entry(void) { return &api; }\n";

static int set_error(char *error, unsigned long capacity, const char *message)
{
    if (error && capacity) snprintf(error, (size_t)capacity, "%s", message);
    return 0;
}

static int make_directory(const char *path)
{
    return mkdir(path, 0755) == 0 || errno == EEXIST;
}

static int path_join(char *output, unsigned long capacity, const char *a, const char *b)
{
    int length = snprintf(output, (size_t)capacity, "%s/%s", a, b);
    return length >= 0 && (unsigned long)length < capacity;
}

static int make_slug(const char *name, char *slug)
{
    unsigned int input = 0;
    unsigned int output = 0;
    int separator = 0;
    while (name && name[input] && output + 1 < MORPH_PROJECT_SLUG_CAPACITY) {
        unsigned char value = (unsigned char)name[input++];
        if (isalnum(value)) {
            if (separator && output) slug[output++] = '-';
            slug[output++] = (char)tolower(value);
            separator = 0;
        } else if (output) {
            separator = 1;
        }
    }
    slug[output] = '\0';
    return output > 0;
}

static int write_file(const char *path, const void *data, size_t size)
{
    FILE *file = fopen(path, "wb");
    int valid;
    if (!file) return 0;
    valid = fwrite(data, 1, size, file) == size && fflush(file) == 0;
    if (fclose(file) != 0) valid = 0;
    return valid;
}

static int compare_entries(const void *left, const void *right)
{
    const morph_project_entry *a = left;
    const morph_project_entry *b = right;
    return strcmp(a->slug, b->slug);
}

static int refresh(morph_project_store *store)
{
    DIR *directory = opendir(store->root);
    struct dirent *entry;
    char active_slug[MORPH_PROJECT_SLUG_CAPACITY] = {0};
    char path[MORPH_PROJECT_PATH_CAPACITY];
    FILE *active;
    store->count = 0;
    store->active_index = 0;
    if (!directory) return 0;
    if (path_join(path, sizeof(path), store->root, ".active-project")) {
        active = fopen(path, "rb");
        if (active) {
            (void)fgets(active_slug, sizeof(active_slug), active);
            active_slug[strcspn(active_slug, "\r\n")] = '\0';
            fclose(active);
        }
    }
    while ((entry = readdir(directory)) != NULL && store->count < MORPH_PROJECT_MAX_PROJECTS) {
        morph_project_entry *project;
        char source[MORPH_PROJECT_PATH_CAPACITY];
        if (entry->d_name[0] == '.' ||
            !path_join(path, sizeof(path), store->root, entry->d_name) ||
            !path_join(source, sizeof(source), path, "app.c")) continue;
        struct stat status;
        if (stat(source, &status) != 0 || !S_ISREG(status.st_mode)) continue;
        project = &store->projects[store->count++];
        snprintf(project->slug, sizeof(project->slug), "%s", entry->d_name);
        snprintf(project->name, sizeof(project->name), "%s", entry->d_name);
        if (path_join(path, sizeof(path), store->root, entry->d_name) &&
            path_join(source, sizeof(source), path, "project.name")) {
            FILE *name_file = fopen(source, "rb");
            if (name_file) {
                (void)fgets(project->name, sizeof(project->name), name_file);
                project->name[strcspn(project->name, "\r\n")] = '\0';
                fclose(name_file);
            }
        }
    }
    closedir(directory);
    qsort(store->projects, store->count, sizeof(store->projects[0]), compare_entries);
    for (unsigned int index = 0; index < store->count; ++index) {
        if (strcmp(store->projects[index].slug, active_slug) == 0) store->active_index = index;
    }
    return 1;
}

int morph_project_store_init(
    morph_project_store *store,
    const char *root,
    char *error,
    unsigned long error_capacity)
{
    if (!store || !root || !*root) return set_error(error, error_capacity, "Project root is invalid");
    memset(store, 0, sizeof(*store));
    if (snprintf(store->root, sizeof(store->root), "%s", root) >= (int)sizeof(store->root) ||
        !make_directory(store->root) || !refresh(store)) {
        return set_error(error, error_capacity, "Unable to initialize the project directory");
    }
    if (!store->count && !morph_project_store_create(store, "My App", error, error_capacity)) return 0;
    return 1;
}

int morph_project_store_create(
    morph_project_store *store,
    const char *name,
    char *error,
    unsigned long error_capacity)
{
    char slug[MORPH_PROJECT_SLUG_CAPACITY];
    char workspace[MORPH_PROJECT_PATH_CAPACITY];
    char path[MORPH_PROJECT_PATH_CAPACITY];
    if (!store || !make_slug(name, slug)) return set_error(error, error_capacity, "Enter a project name");
    if (!path_join(workspace, sizeof(workspace), store->root, slug) || mkdir(workspace, 0755) != 0) {
        return set_error(error, error_capacity, errno == EEXIST
            ? "A project with that name already exists" : "Unable to create the project");
    }
    if (!path_join(path, sizeof(path), workspace, "assets") || !make_directory(path) ||
        !path_join(path, sizeof(path), workspace, "agent") || !make_directory(path) ||
        !path_join(path, sizeof(path), workspace, "project.name") ||
        !write_file(path, name, strlen(name)) ||
        !path_join(path, sizeof(path), workspace, "app.c") ||
        !write_file(path, starter_source, sizeof(starter_source) - 1)) {
        return set_error(error, error_capacity, "Unable to create starter project files");
    }
    if (!refresh(store)) return set_error(error, error_capacity, "Unable to refresh projects");
    for (unsigned int index = 0; index < store->count; ++index) {
        if (strcmp(store->projects[index].slug, slug) == 0) {
            snprintf(store->projects[index].name, sizeof(store->projects[index].name), "%s", name);
            return morph_project_store_select(store, index, error, error_capacity);
        }
    }
    return set_error(error, error_capacity, "New project was not found after creation");
}

int morph_project_store_select(
    morph_project_store *store,
    unsigned int index,
    char *error,
    unsigned long error_capacity)
{
    char path[MORPH_PROJECT_PATH_CAPACITY];
    if (!store || index >= store->count ||
        !path_join(path, sizeof(path), store->root, ".active-project") ||
        !write_file(path, store->projects[index].slug, strlen(store->projects[index].slug))) {
        return set_error(error, error_capacity, "Unable to select the project");
    }
    store->active_index = index;
    return 1;
}

int morph_project_store_paths(
    const morph_project_store *store,
    char *workspace,
    unsigned long workspace_capacity,
    char *source,
    unsigned long source_capacity,
    char *assets,
    unsigned long assets_capacity)
{
    const char *slug;
    if (!store || !store->count || store->active_index >= store->count) return 0;
    slug = store->projects[store->active_index].slug;
    return path_join(workspace, workspace_capacity, store->root, slug) &&
        path_join(source, source_capacity, workspace, "app.c") &&
        path_join(assets, assets_capacity, workspace, "assets");
}
