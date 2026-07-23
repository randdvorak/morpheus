#include "morpheus/app_api.h"

typedef struct database_state {
    const morph_database_api *api;
    void *context;
    int ready;
} database_state;

static database_state storage;

static int create(morph_host *host, void **state)
{
    static const char schema[] =
        "CREATE TABLE generated_value (value INTEGER NOT NULL) STRICT;";
    static const morph_db_migration migrations[] = {
        {1, schema, sizeof(schema) - 1u}
    };
    static const char insert[] =
        "INSERT INTO generated_value(value) VALUES(42)";
    const morph_capability *capability = morph_host_find_capability(
        host, MORPHEUS_DATABASE_CAPABILITY, MORPHEUS_DATABASE_ABI_VERSION);
    storage.api = morph_database_from_capability(capability);
    storage.context = capability ? capability->context : 0;
    storage.ready = storage.api && storage.context &&
        storage.api->migrate(storage.context, migrations, 1, 1) == MORPH_DB_OK &&
        storage.api->execute(storage.context, insert, sizeof(insert) - 1u) ==
            MORPH_DB_OK;
    *state = &storage;
    return storage.ready;
}

static void render(morph_host *host, void *opaque)
{
    static const char query[] =
        "SELECT value FROM generated_value ORDER BY rowid DESC LIMIT 1";
    database_state *state = opaque;
    morph_db_statement statement = 0;
    const char *label = "TinyCC database unavailable";
    if (state->ready &&
        state->api->prepare(state->context, query, sizeof(query) - 1u,
            &statement) == MORPH_DB_OK &&
        state->api->step(state->context, statement) == MORPH_DB_ROW &&
        state->api->column_integer(state->context, statement, 0) == 42) {
        label = "TinyCC database available";
    }
    if (statement) state->api->finalize(state->context, statement);
    host->ui_label(host, label);
}

static const morph_app_api app_api = {
    MORPHEUS_APP_ABI_VERSION,
    "database-smoke",
    create,
    0,
    0,
    render,
    0,
    0
};

const morph_app_api *morph_app_entry(void)
{
    return &app_api;
}
