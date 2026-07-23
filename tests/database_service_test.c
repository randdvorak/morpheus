#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "database_service.h"

static int fail(
    const morph_database_api *api, void *context, int code, const char *stage)
{
    morph_db_error error = {0};
    if (api) api->last_error(context, &error);
    fprintf(stderr, "%s failed: status=%d backend=%d message=%s\n",
        stage, error.status, error.backend_code, error.message);
    return code;
}

static int query_integer(
    const morph_database_api *api, void *context,
    const char *sql, unsigned long sql_size, long long *value)
{
    morph_db_statement statement = 0;
    int ready = api->prepare(context, sql, sql_size, &statement) == MORPH_DB_OK &&
        api->step(context, statement) == MORPH_DB_ROW;
    if (ready) *value = api->column_integer(context, statement, 0);
    if (statement) api->finalize(context, statement);
    return ready;
}

static void remove_database_files(const char *path)
{
    char sidecar[MORPHEUS_DATABASE_PATH_CAPACITY + 16u];
    const char *suffixes[] = {"", "-wal", "-shm", ".preview",
        ".preview-wal", ".preview-shm"};
    unsigned int index;
    for (index = 0; index < sizeof(suffixes) / sizeof(suffixes[0]); ++index) {
        snprintf(sidecar, sizeof(sidecar), "%s%s", path, suffixes[index]);
        (void)remove(sidecar);
    }
}

int main(void)
{
    static const char schema_v1[] =
        "CREATE TABLE record ("
        "id INTEGER PRIMARY KEY,"
        "name TEXT NOT NULL,"
        "score REAL NOT NULL,"
        "payload BLOB"
        ") STRICT;";
    static const char schema_v2[] =
        "ALTER TABLE record ADD COLUMN enabled INTEGER NOT NULL DEFAULT 1;";
    static const char schema_v3_invalid[] =
        "CREATE TABLE migration_should_rollback(value INTEGER) STRICT;"
        "THIS IS NOT SQL;";
    static const morph_db_migration migrations[] = {
        {1, schema_v1, sizeof(schema_v1) - 1u},
        {2, schema_v2, sizeof(schema_v2) - 1u}
    };
    static const morph_db_migration failing_migrations[] = {
        {1, schema_v1, sizeof(schema_v1) - 1u},
        {2, schema_v2, sizeof(schema_v2) - 1u},
        {3, schema_v3_invalid, sizeof(schema_v3_invalid) - 1u}
    };
    static const unsigned char payload[] = {0, 1, 2, 0xff};
    static const char insert_sql[] =
        "INSERT INTO record(name, score, payload) VALUES(?1, ?2, ?3)";
    static const char select_sql[] =
        "SELECT id, name, score, payload, enabled FROM record WHERE id = ?1";
    static const char attach_sql[] = "ATTACH DATABASE ':memory:' AS other";
    static const char rollback_insert_sql[] =
        "INSERT INTO record(name, score) VALUES('rollback', 1)";
    static const char count_sql[] = "SELECT count(*) FROM record";
    morph_database_service *service;
    morph_capability capability;
    const morph_database_api *api;
    void *context;
    morph_db_statement statement = 0;
    morph_db_statement stale_statement;
    morph_db_status status;
    morph_db_bytes bytes;
    char error[192] = {0};
    long long row_id;
    char path[] = "/tmp/morpheus-database-XXXXXX";
    int descriptor;
    long long value = 0;

    service = morph_database_service_create(":memory:", error, sizeof(error));
    if (!service) {
        fprintf(stderr, "database create failed: %s\n", error);
        return 1;
    }
    capability = morph_database_service_capability(service);
    api = morph_database_from_capability(&capability);
    context = capability.context;
    if (!api || !context) return 2;

    status = api->migrate(context, migrations, 2, 2);
    if (status != MORPH_DB_OK) return fail(api, context, 3, "migration");
    if (api->migrate(context, migrations, 2, 2) != MORPH_DB_OK)
        return fail(api, context, 4, "idempotent migration");
    if (api->migrate(context, migrations, 2, 1) != MORPH_DB_SCHEMA_NEWER)
        return fail(api, context, 5, "newer schema detection");
    if (api->migrate(context, failing_migrations, 3, 3) != MORPH_DB_ERROR)
        return fail(api, context, 25, "failing migration");
    if (!query_integer(api, context, "PRAGMA user_version",
            sizeof("PRAGMA user_version") - 1u, &value) || value != 2 ||
        !query_integer(api, context,
            "SELECT count(*) FROM sqlite_master "
            "WHERE name='migration_should_rollback'",
            sizeof("SELECT count(*) FROM sqlite_master "
                "WHERE name='migration_should_rollback'") - 1u,
            &value) || value != 0)
        return fail(api, context, 26, "migration rollback");

    if (api->prepare(context, insert_sql, sizeof(insert_sql) - 1u,
            &statement) != MORPH_DB_OK ||
        api->bind_text(context, statement, 1, "Morpheus", 8) != MORPH_DB_OK ||
        api->bind_real(context, statement, 2, 42.5) != MORPH_DB_OK ||
        api->bind_blob(context, statement, 3,
            payload, sizeof(payload)) != MORPH_DB_OK ||
        api->step(context, statement) != MORPH_DB_DONE)
        return fail(api, context, 6, "insert");
    row_id = api->last_insert_rowid(context);
    if (row_id <= 0 || api->changes(context) != 1) return 7;
    stale_statement = statement;
    api->finalize(context, statement);

    if (api->prepare(context, select_sql, sizeof(select_sql) - 1u,
            &statement) != MORPH_DB_OK ||
        api->bind_integer(context, statement, 1, row_id) != MORPH_DB_OK ||
        api->step(context, statement) != MORPH_DB_ROW ||
        api->column_count(context, statement) != 5 ||
        api->column_type(context, statement, 0) != MORPH_DB_INTEGER ||
        api->column_integer(context, statement, 0) != row_id ||
        api->column_type(context, statement, 1) != MORPH_DB_TEXT)
        return fail(api, context, 8, "select");
    bytes = api->column_text(context, statement, 1);
    if (bytes.size != 8 || memcmp(bytes.data, "Morpheus", 8) != 0 ||
        api->column_real(context, statement, 2) != 42.5)
        return 9;
    bytes = api->column_blob(context, statement, 3);
    if (bytes.size != sizeof(payload) ||
        memcmp(bytes.data, payload, sizeof(payload)) != 0 ||
        api->column_integer(context, statement, 4) != 1)
        return 10;
    if (api->step(context, statement) != MORPH_DB_DONE) return 11;
    api->finalize(context, statement);

    if (api->reset(context, stale_statement, 1) != MORPH_DB_INVALID)
        return 12;
    if (api->execute(context, "SELECT 1; SELECT 2", 18) != MORPH_DB_INVALID)
        return 13;
    if (api->execute(context,
            attach_sql, sizeof(attach_sql) - 1u) != MORPH_DB_ERROR)
        return 14;
    if (api->execute(context, "PRAGMA foreign_keys = OFF",
            sizeof("PRAGMA foreign_keys = OFF") - 1u) != MORPH_DB_ERROR)
        return 24;
    if (api->execute(context, "PRAGMA FOREIGN_KEYS = OFF",
            sizeof("PRAGMA FOREIGN_KEYS = OFF") - 1u) != MORPH_DB_ERROR)
        return 27;

    if (api->begin(context) != MORPH_DB_OK ||
        api->execute(context,
            rollback_insert_sql, sizeof(rollback_insert_sql) - 1u) !=
                MORPH_DB_OK)
        return fail(api, context, 15, "transaction insert");
    api->rollback(context);
    if (api->prepare(context, count_sql,
            sizeof(count_sql) - 1u, &statement) != MORPH_DB_OK ||
        api->step(context, statement) != MORPH_DB_ROW ||
        api->column_integer(context, statement, 0) != 1)
        return fail(api, context, 16, "transaction rollback");
    api->finalize(context, statement);

    morph_database_service_destroy(service);

    descriptor = mkstemp(path);
    if (descriptor < 0) return 17;
    close(descriptor);
    remove_database_files(path);
    service = morph_database_service_create(path, error, sizeof(error));
    if (!service) return 18;
    capability = morph_database_service_capability(service);
    api = morph_database_from_capability(&capability);
    context = capability.context;
    if (api->execute(context,
            "CREATE TABLE preview_value(value INTEGER NOT NULL) STRICT",
            sizeof("CREATE TABLE preview_value(value INTEGER NOT NULL) STRICT") - 1u) !=
            MORPH_DB_OK ||
        api->execute(context, "INSERT INTO preview_value VALUES(1)",
            sizeof("INSERT INTO preview_value VALUES(1)") - 1u) != MORPH_DB_OK)
        return fail(api, context, 19, "preview fixture");
    if (!morph_database_service_begin_preview(service, error, sizeof(error)) ||
        api->execute(context, "UPDATE preview_value SET value=2",
            sizeof("UPDATE preview_value SET value=2") - 1u) != MORPH_DB_OK ||
        !morph_database_service_reject_preview(service, error, sizeof(error)) ||
        !query_integer(api, context, "SELECT value FROM preview_value",
            sizeof("SELECT value FROM preview_value") - 1u, &value) || value != 1)
        return fail(api, context, 20, "preview rejection");
    if (!morph_database_service_begin_preview(service, error, sizeof(error)) ||
        api->execute(context, "UPDATE preview_value SET value=3",
            sizeof("UPDATE preview_value SET value=3") - 1u) != MORPH_DB_OK ||
        !morph_database_service_accept_preview(service, error, sizeof(error)) ||
        !query_integer(api, context, "SELECT value FROM preview_value",
            sizeof("SELECT value FROM preview_value") - 1u, &value) || value != 3)
        return fail(api, context, 21, "preview acceptance");
    morph_database_service_destroy(service);
    service = morph_database_service_create(path, error, sizeof(error));
    if (!service) return 22;
    capability = morph_database_service_capability(service);
    api = morph_database_from_capability(&capability);
    context = capability.context;
    if (!query_integer(api, context, "SELECT value FROM preview_value",
            sizeof("SELECT value FROM preview_value") - 1u, &value) || value != 3)
        return fail(api, context, 23, "accepted database reopen");
    morph_database_service_destroy(service);
    remove_database_files(path);
    puts("PASS: bounded database capability, migrations, CRUD, and transactions");
    return 0;
}
