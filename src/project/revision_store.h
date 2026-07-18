#ifndef MORPHEUS_REVISION_STORE_H
#define MORPHEUS_REVISION_STORE_H

#define MORPH_REVISION_PATH_CAPACITY 4096

typedef struct morph_revision_store {
    char workspace_root[MORPH_REVISION_PATH_CAPACITY];
    unsigned long active_revision;
    unsigned long latest_revision;
} morph_revision_store;

void morph_revision_store_reset(morph_revision_store *store);
int morph_revision_store_init(
    morph_revision_store *store,
    const char *workspace_root,
    char *error,
    unsigned long error_capacity);
int morph_revision_store_checkpoint(
    morph_revision_store *store,
    const char *source_path,
    const void *state_data,
    unsigned long state_size,
    unsigned long *revision,
    char *error,
    unsigned long error_capacity);
int morph_revision_store_load(
    const morph_revision_store *store,
    unsigned long revision,
    char *source_path,
    unsigned long source_path_capacity,
    void **state_data,
    unsigned long *state_size,
    char *error,
    unsigned long error_capacity);
int morph_revision_store_previous(
    const morph_revision_store *store,
    unsigned long *revision);
int morph_revision_store_set_active(
    morph_revision_store *store,
    unsigned long revision,
    char *error,
    unsigned long error_capacity);
int morph_revision_store_begin_session(
    morph_revision_store *store,
    int *recovered_from_crash,
    char *error,
    unsigned long error_capacity);
int morph_revision_store_refresh_session(
    const morph_revision_store *store,
    char *error,
    unsigned long error_capacity);
int morph_revision_store_end_session(
    const morph_revision_store *store,
    char *error,
    unsigned long error_capacity);
int morph_revision_store_record_attempt(
    const morph_revision_store *store,
    const char *stage,
    int succeeded,
    const char *message,
    char *error,
    unsigned long error_capacity);
void morph_revision_store_release_state(void *state_data);

#endif
