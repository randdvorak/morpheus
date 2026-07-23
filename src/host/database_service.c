#include "database_service.h"

#include <ctype.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sqlite3.h"

#define MORPH_DB_QUERY_BUDGET_NS 250000000ull
#define MORPH_DB_MIGRATION_BUDGET_NS 5000000000ull

typedef struct morph_database_statement_slot {
    sqlite3_stmt *statement;
    unsigned int generation;
} morph_database_statement_slot;

struct morph_database_service {
    sqlite3 *database;
    morph_database_statement_slot statements[MORPHEUS_DATABASE_MAX_STATEMENTS];
    morph_db_error error;
    pthread_t owner_thread;
    uint64_t deadline_ns;
    int deadline_active;
    int transaction_active;
    int allow_transaction_sql;
    int allow_sensitive_pragma;
    int preview_active;
    char accepted_path[MORPHEUS_DATABASE_PATH_CAPACITY];
    char active_path[MORPHEUS_DATABASE_PATH_CAPACITY];
};

static uint64_t morph_database_now_ns(void)
{
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) return 0;
    return (uint64_t)value.tv_sec * 1000000000ull + (uint64_t)value.tv_nsec;
}

static void morph_database_deadline_begin(
    morph_database_service *service, uint64_t budget)
{
    uint64_t now = morph_database_now_ns();
    service->deadline_ns = now ? now + budget : 0;
    service->deadline_active = service->deadline_ns != 0;
}

static void morph_database_deadline_end(morph_database_service *service)
{
    service->deadline_active = 0;
    service->deadline_ns = 0;
}

static int morph_database_progress(void *context)
{
    morph_database_service *service = context;
    return service && service->deadline_active &&
        morph_database_now_ns() >= service->deadline_ns;
}

static morph_db_status morph_database_status(int result)
{
    switch (result & 0xff) {
    case SQLITE_OK: return MORPH_DB_OK;
    case SQLITE_ROW: return MORPH_DB_ROW;
    case SQLITE_DONE: return MORPH_DB_DONE;
    case SQLITE_BUSY:
    case SQLITE_LOCKED: return MORPH_DB_BUSY;
    case SQLITE_CONSTRAINT: return MORPH_DB_CONSTRAINT;
    case SQLITE_TOOBIG:
    case SQLITE_FULL: return MORPH_DB_TOO_LARGE;
    case SQLITE_INTERRUPT: return MORPH_DB_INTERRUPTED;
    case SQLITE_MISUSE:
    case SQLITE_RANGE: return MORPH_DB_INVALID;
    default: return MORPH_DB_ERROR;
    }
}

static morph_db_status morph_database_set_error(
    morph_database_service *service, int result, const char *message)
{
    morph_db_status status = morph_database_status(result);
    if (!service) return status;
    service->error.status = status;
    service->error.backend_code = result;
    snprintf(service->error.message, sizeof(service->error.message), "%s",
        message && *message ? message :
        service->database ? sqlite3_errmsg(service->database) : "Database unavailable");
    return status;
}

static void morph_database_clear_error(morph_database_service *service)
{
    if (!service) return;
    memset(&service->error, 0, sizeof(service->error));
    service->error.status = MORPH_DB_OK;
}

static int morph_database_valid_thread(morph_database_service *service)
{
    if (service && pthread_equal(service->owner_thread, pthread_self())) return 1;
    if (service) (void)morph_database_set_error(
        service, SQLITE_MISUSE, "Database calls must remain on the runtime thread");
    return 0;
}

static int morph_database_name_is(const char *value, const char *expected)
{
    return value && sqlite3_stricmp(value, expected) == 0;
}

static int morph_database_authorize(
    void *context, int action, const char *first, const char *second,
    const char *database_name, const char *trigger_name)
{
    morph_database_service *service = context;
    (void)database_name;
    (void)trigger_name;
    if (action == SQLITE_ATTACH || action == SQLITE_DETACH) return SQLITE_DENY;
    if ((action == SQLITE_TRANSACTION || action == SQLITE_SAVEPOINT) &&
        !service->allow_transaction_sql) return SQLITE_DENY;
    if (action == SQLITE_PRAGMA && first) {
        int is_sensitive = morph_database_name_is(first, "writable_schema") ||
            morph_database_name_is(first, "temp_store_directory") ||
            morph_database_name_is(first, "data_store_directory");
        int locks_policy = morph_database_name_is(first, "foreign_keys") ||
            morph_database_name_is(first, "defer_foreign_keys") ||
            morph_database_name_is(first, "journal_mode") ||
            morph_database_name_is(first, "synchronous") ||
            morph_database_name_is(first, "trusted_schema") ||
            morph_database_name_is(first, "recursive_triggers") ||
            morph_database_name_is(first, "locking_mode") ||
            morph_database_name_is(first, "query_only") ||
            morph_database_name_is(first, "auto_vacuum") ||
            morph_database_name_is(first, "user_version") ||
            morph_database_name_is(first, "application_id") ||
            morph_database_name_is(first, "schema_version") ||
            morph_database_name_is(first, "max_page_count") ||
            morph_database_name_is(first, "page_size");
        int writes_policy = locks_policy && second && *second;
        if ((is_sensitive || writes_policy) &&
            !service->allow_sensitive_pragma) return SQLITE_DENY;
    }
    if (action == SQLITE_FUNCTION &&
        (morph_database_name_is(second, "load_extension") ||
         morph_database_name_is(second, "readfile") ||
         morph_database_name_is(second, "writefile"))) return SQLITE_DENY;
    return SQLITE_OK;
}

static int morph_database_trailing_sql_is_empty(
    const char *tail, const char *end)
{
    while (tail < end) {
        if (!isspace((unsigned char)*tail)) return 0;
        ++tail;
    }
    return 1;
}

static int morph_database_sql_valid(
    morph_database_service *service, const char *sql, unsigned long size)
{
    if (!morph_database_valid_thread(service)) return 0;
    if (!sql || !size || size > MORPHEUS_DATABASE_MAX_SQL || size > INT_MAX) {
        (void)morph_database_set_error(
            service, SQLITE_TOOBIG, "SQL is empty or exceeds the 256 KiB limit");
        return 0;
    }
    return 1;
}

static sqlite3_stmt *morph_database_prepare_raw(
    morph_database_service *service, const char *sql, unsigned long size,
    uint64_t budget)
{
    sqlite3_stmt *statement = NULL;
    const char *tail = NULL;
    int result;
    if (!morph_database_sql_valid(service, sql, size)) return NULL;
    morph_database_deadline_begin(service, budget);
    result = sqlite3_prepare_v3(service->database, sql, (int)size,
        SQLITE_PREPARE_PERSISTENT, &statement, &tail);
    morph_database_deadline_end(service);
    if (result != SQLITE_OK) {
        (void)morph_database_set_error(service, result, NULL);
        if (statement) sqlite3_finalize(statement);
        return NULL;
    }
    if (!statement || !morph_database_trailing_sql_is_empty(tail, sql + size)) {
        if (statement) sqlite3_finalize(statement);
        (void)morph_database_set_error(
            service, SQLITE_MISUSE, "Exactly one SQL statement is required");
        return NULL;
    }
    morph_database_clear_error(service);
    return statement;
}

static morph_db_statement morph_database_statement_id(
    unsigned int index, unsigned int generation)
{
    return ((morph_db_statement)generation << 32u) |
        (morph_db_statement)(index + 1u);
}

static morph_database_statement_slot *morph_database_statement_find(
    morph_database_service *service, morph_db_statement id)
{
    unsigned int index;
    unsigned int generation;
    morph_database_statement_slot *slot;
    if (!morph_database_valid_thread(service) || !id) return NULL;
    index = (unsigned int)(id & 0xffffffffu);
    generation = (unsigned int)(id >> 32u);
    if (!index || index > MORPHEUS_DATABASE_MAX_STATEMENTS || !generation) {
        (void)morph_database_set_error(service, SQLITE_MISUSE,
            "Database statement handle is invalid");
        return NULL;
    }
    slot = &service->statements[index - 1u];
    if (!slot->statement || slot->generation != generation) {
        (void)morph_database_set_error(service, SQLITE_MISUSE,
            "Database statement handle is stale");
        return NULL;
    }
    return slot;
}

static morph_db_status morph_database_execute_api(
    void *context, const char *sql, unsigned long sql_size)
{
    morph_database_service *service = context;
    sqlite3_stmt *statement = morph_database_prepare_raw(
        service, sql, sql_size, MORPH_DB_QUERY_BUDGET_NS);
    int step_result;
    int finalize_result;
    if (!statement) return service ? service->error.status : MORPH_DB_INVALID;
    morph_database_deadline_begin(service, MORPH_DB_QUERY_BUDGET_NS);
    step_result = sqlite3_step(statement);
    morph_database_deadline_end(service);
    finalize_result = sqlite3_finalize(statement);
    if (step_result != SQLITE_DONE) return morph_database_set_error(
        service, step_result == SQLITE_ROW ? SQLITE_MISUSE : step_result,
        step_result == SQLITE_ROW ? "execute cannot return result rows" : NULL);
    if (finalize_result != SQLITE_OK)
        return morph_database_set_error(service, finalize_result, NULL);
    morph_database_clear_error(service);
    return MORPH_DB_OK;
}

static morph_db_status morph_database_prepare_api(
    void *context, const char *sql, unsigned long sql_size,
    morph_db_statement *output)
{
    morph_database_service *service = context;
    sqlite3_stmt *statement;
    unsigned int index;
    if (!output) return morph_database_set_error(
        service, SQLITE_MISUSE, "Statement output is required");
    *output = 0;
    statement = morph_database_prepare_raw(
        service, sql, sql_size, MORPH_DB_QUERY_BUDGET_NS);
    if (!statement) return service ? service->error.status : MORPH_DB_INVALID;
    for (index = 0; index < MORPHEUS_DATABASE_MAX_STATEMENTS; ++index) {
        morph_database_statement_slot *slot = &service->statements[index];
        if (slot->statement) continue;
        slot->generation++;
        if (!slot->generation) slot->generation++;
        slot->statement = statement;
        *output = morph_database_statement_id(index, slot->generation);
        morph_database_clear_error(service);
        return MORPH_DB_OK;
    }
    sqlite3_finalize(statement);
    return morph_database_set_error(
        service, SQLITE_FULL, "The 64-statement database limit was reached");
}

static void morph_database_finalize_api(void *context, morph_db_statement id)
{
    morph_database_service *service = context;
    morph_database_statement_slot *slot = morph_database_statement_find(service, id);
    if (!slot) return;
    (void)sqlite3_finalize(slot->statement);
    slot->statement = NULL;
    morph_database_clear_error(service);
}

static morph_db_status morph_database_reset_api(
    void *context, morph_db_statement id, int clear_bindings)
{
    morph_database_service *service = context;
    morph_database_statement_slot *slot = morph_database_statement_find(service, id);
    int result;
    if (!slot) return service ? service->error.status : MORPH_DB_INVALID;
    result = sqlite3_reset(slot->statement);
    if (result == SQLITE_OK && clear_bindings)
        result = sqlite3_clear_bindings(slot->statement);
    if (result != SQLITE_OK) return morph_database_set_error(service, result, NULL);
    morph_database_clear_error(service);
    return MORPH_DB_OK;
}

static morph_db_status morph_database_bind_result(
    morph_database_service *service, int result)
{
    if (result != SQLITE_OK) return morph_database_set_error(service, result, NULL);
    morph_database_clear_error(service);
    return MORPH_DB_OK;
}

static morph_db_status morph_database_bind_null_api(
    void *context, morph_db_statement id, unsigned int index)
{
    morph_database_service *service = context;
    morph_database_statement_slot *slot = morph_database_statement_find(service, id);
    if (!slot || index > INT_MAX) return service ? service->error.status : MORPH_DB_INVALID;
    return morph_database_bind_result(
        service, sqlite3_bind_null(slot->statement, (int)index));
}

static morph_db_status morph_database_bind_integer_api(
    void *context, morph_db_statement id, unsigned int index, long long value)
{
    morph_database_service *service = context;
    morph_database_statement_slot *slot = morph_database_statement_find(service, id);
    if (!slot || index > INT_MAX) return service ? service->error.status : MORPH_DB_INVALID;
    return morph_database_bind_result(service,
        sqlite3_bind_int64(slot->statement, (int)index, (sqlite3_int64)value));
}

static morph_db_status morph_database_bind_real_api(
    void *context, morph_db_statement id, unsigned int index, double value)
{
    morph_database_service *service = context;
    morph_database_statement_slot *slot = morph_database_statement_find(service, id);
    if (!slot || index > INT_MAX) return service ? service->error.status : MORPH_DB_INVALID;
    return morph_database_bind_result(
        service, sqlite3_bind_double(slot->statement, (int)index, value));
}

static int morph_database_binding_valid(
    morph_database_service *service, const void *value, unsigned long size)
{
    if ((!value && size) || size > MORPHEUS_DATABASE_MAX_VALUE || size > INT_MAX) {
        (void)morph_database_set_error(service, SQLITE_TOOBIG,
            "Database value exceeds the 16 MiB limit");
        return 0;
    }
    return 1;
}

static morph_db_status morph_database_bind_text_api(
    void *context, morph_db_statement id, unsigned int index,
    const char *value, unsigned long size)
{
    morph_database_service *service = context;
    morph_database_statement_slot *slot = morph_database_statement_find(service, id);
    if (!slot || index > INT_MAX ||
        !morph_database_binding_valid(service, value, size))
        return service ? service->error.status : MORPH_DB_INVALID;
    return morph_database_bind_result(service, sqlite3_bind_text(
        slot->statement, (int)index, value ? value : "", (int)size, SQLITE_TRANSIENT));
}

static morph_db_status morph_database_bind_blob_api(
    void *context, morph_db_statement id, unsigned int index,
    const void *value, unsigned long size)
{
    morph_database_service *service = context;
    morph_database_statement_slot *slot = morph_database_statement_find(service, id);
    if (!slot || index > INT_MAX ||
        !morph_database_binding_valid(service, value, size))
        return service ? service->error.status : MORPH_DB_INVALID;
    return morph_database_bind_result(service, sqlite3_bind_blob(
        slot->statement, (int)index, value ? value : "",
        (int)size, SQLITE_TRANSIENT));
}

static morph_db_status morph_database_step_api(
    void *context, morph_db_statement id)
{
    morph_database_service *service = context;
    morph_database_statement_slot *slot = morph_database_statement_find(service, id);
    int result;
    if (!slot) return service ? service->error.status : MORPH_DB_INVALID;
    morph_database_deadline_begin(service, MORPH_DB_QUERY_BUDGET_NS);
    result = sqlite3_step(slot->statement);
    morph_database_deadline_end(service);
    if (result != SQLITE_ROW && result != SQLITE_DONE)
        return morph_database_set_error(service, result, NULL);
    morph_database_clear_error(service);
    return morph_database_status(result);
}

static int morph_database_column_valid(
    morph_database_service *service, sqlite3_stmt *statement, unsigned int column)
{
    if (column < (unsigned int)sqlite3_column_count(statement)) return 1;
    (void)morph_database_set_error(
        service, SQLITE_RANGE, "Database column index is out of range");
    return 0;
}

static unsigned int morph_database_column_count_api(
    void *context, morph_db_statement id)
{
    morph_database_service *service = context;
    morph_database_statement_slot *slot = morph_database_statement_find(service, id);
    return slot ? (unsigned int)sqlite3_column_count(slot->statement) : 0;
}

static morph_db_value_type morph_database_column_type_api(
    void *context, morph_db_statement id, unsigned int column)
{
    morph_database_service *service = context;
    morph_database_statement_slot *slot = morph_database_statement_find(service, id);
    int type;
    if (!slot || !morph_database_column_valid(service, slot->statement, column))
        return MORPH_DB_NULL;
    type = sqlite3_column_type(slot->statement, (int)column);
    switch (type) {
    case SQLITE_INTEGER: return MORPH_DB_INTEGER;
    case SQLITE_FLOAT: return MORPH_DB_REAL;
    case SQLITE_TEXT: return MORPH_DB_TEXT;
    case SQLITE_BLOB: return MORPH_DB_BLOB;
    default: return MORPH_DB_NULL;
    }
}

static long long morph_database_column_integer_api(
    void *context, morph_db_statement id, unsigned int column)
{
    morph_database_service *service = context;
    morph_database_statement_slot *slot = morph_database_statement_find(service, id);
    if (!slot || !morph_database_column_valid(service, slot->statement, column)) return 0;
    return (long long)sqlite3_column_int64(slot->statement, (int)column);
}

static double morph_database_column_real_api(
    void *context, morph_db_statement id, unsigned int column)
{
    morph_database_service *service = context;
    morph_database_statement_slot *slot = morph_database_statement_find(service, id);
    if (!slot || !morph_database_column_valid(service, slot->statement, column)) return 0;
    return sqlite3_column_double(slot->statement, (int)column);
}

static morph_db_bytes morph_database_column_bytes(
    morph_database_service *service, morph_db_statement id,
    unsigned int column, int text)
{
    morph_db_bytes output = {0};
    morph_database_statement_slot *slot = morph_database_statement_find(service, id);
    int size;
    if (!slot || !morph_database_column_valid(service, slot->statement, column))
        return output;
    output.data = text
        ? (const void *)sqlite3_column_text(slot->statement, (int)column)
        : sqlite3_column_blob(slot->statement, (int)column);
    size = sqlite3_column_bytes(slot->statement, (int)column);
    output.size = size > 0 ? (unsigned long)size : 0;
    return output;
}

static morph_db_bytes morph_database_column_text_api(
    void *context, morph_db_statement id, unsigned int column)
{
    return morph_database_column_bytes(context, id, column, 1);
}

static morph_db_bytes morph_database_column_blob_api(
    void *context, morph_db_statement id, unsigned int column)
{
    return morph_database_column_bytes(context, id, column, 0);
}

static long long morph_database_last_insert_rowid_api(void *context)
{
    morph_database_service *service = context;
    if (!morph_database_valid_thread(service)) return 0;
    return (long long)sqlite3_last_insert_rowid(service->database);
}

static unsigned long morph_database_changes_api(void *context)
{
    morph_database_service *service = context;
    int changes;
    if (!morph_database_valid_thread(service)) return 0;
    changes = sqlite3_changes(service->database);
    return changes > 0 ? (unsigned long)changes : 0;
}

static morph_db_status morph_database_transaction_sql(
    morph_database_service *service, const char *sql)
{
    char *message = NULL;
    int result;
    service->allow_transaction_sql = 1;
    morph_database_deadline_begin(service, MORPH_DB_QUERY_BUDGET_NS);
    result = sqlite3_exec(service->database, sql, NULL, NULL, &message);
    morph_database_deadline_end(service);
    service->allow_transaction_sql = 0;
    if (result != SQLITE_OK) {
        morph_db_status status = morph_database_set_error(service, result, message);
        sqlite3_free(message);
        return status;
    }
    sqlite3_free(message);
    morph_database_clear_error(service);
    return MORPH_DB_OK;
}

static morph_db_status morph_database_begin_api(void *context)
{
    morph_database_service *service = context;
    morph_db_status status;
    if (!morph_database_valid_thread(service)) return MORPH_DB_INVALID;
    if (service->transaction_active) return morph_database_set_error(
        service, SQLITE_MISUSE, "A database transaction is already active");
    status = morph_database_transaction_sql(service, "BEGIN IMMEDIATE");
    if (status == MORPH_DB_OK) service->transaction_active = 1;
    return status;
}

static morph_db_status morph_database_commit_api(void *context)
{
    morph_database_service *service = context;
    morph_db_status status;
    if (!morph_database_valid_thread(service)) return MORPH_DB_INVALID;
    if (!service->transaction_active) return morph_database_set_error(
        service, SQLITE_MISUSE, "No database transaction is active");
    status = morph_database_transaction_sql(service, "COMMIT");
    if (status == MORPH_DB_OK) service->transaction_active = 0;
    return status;
}

static void morph_database_rollback_api(void *context)
{
    morph_database_service *service = context;
    if (!morph_database_valid_thread(service) || !service->transaction_active) return;
    if (morph_database_transaction_sql(service, "ROLLBACK") == MORPH_DB_OK)
        service->transaction_active = 0;
}

static int morph_database_read_user_version(
    morph_database_service *service, unsigned int *version)
{
    sqlite3_stmt *statement;
    int result;
    static const char sql[] = "PRAGMA user_version";
    statement = morph_database_prepare_raw(
        service, sql, sizeof(sql) - 1u, MORPH_DB_QUERY_BUDGET_NS);
    if (!statement) return 0;
    result = sqlite3_step(statement);
    if (result == SQLITE_ROW) {
        sqlite3_int64 value = sqlite3_column_int64(statement, 0);
        if (value >= 0 && value <= UINT_MAX) *version = (unsigned int)value;
        else result = SQLITE_RANGE;
    }
    if (result == SQLITE_ROW) result = SQLITE_OK;
    if (sqlite3_finalize(statement) != SQLITE_OK && result == SQLITE_OK)
        result = sqlite3_errcode(service->database);
    if (result != SQLITE_OK) {
        (void)morph_database_set_error(service, result, NULL);
        return 0;
    }
    return 1;
}

static int morph_database_run_script(
    morph_database_service *service, const char *sql, unsigned long size)
{
    char *copy;
    char *message = NULL;
    int result;
    if (!sql || !size || size > MORPHEUS_DATABASE_MAX_SQL || size > INT_MAX) {
        (void)morph_database_set_error(service, SQLITE_TOOBIG,
            "Migration SQL is empty or exceeds the 256 KiB limit");
        return 0;
    }
    copy = malloc((size_t)size + 1u);
    if (!copy) {
        (void)morph_database_set_error(service, SQLITE_NOMEM, NULL);
        return 0;
    }
    memcpy(copy, sql, (size_t)size);
    copy[size] = '\0';
    morph_database_deadline_begin(service, MORPH_DB_MIGRATION_BUDGET_NS);
    result = sqlite3_exec(service->database, copy, NULL, NULL, &message);
    morph_database_deadline_end(service);
    free(copy);
    if (result != SQLITE_OK) {
        (void)morph_database_set_error(service, result, message);
        sqlite3_free(message);
        return 0;
    }
    sqlite3_free(message);
    return 1;
}

static int morph_database_write_user_version(
    morph_database_service *service, unsigned int version)
{
    char sql[64];
    char *message = NULL;
    int result;
    snprintf(sql, sizeof(sql), "PRAGMA user_version = %u", version);
    service->allow_sensitive_pragma = 1;
    result = sqlite3_exec(service->database, sql, NULL, NULL, &message);
    service->allow_sensitive_pragma = 0;
    if (result != SQLITE_OK) {
        (void)morph_database_set_error(service, result, message);
        sqlite3_free(message);
        return 0;
    }
    sqlite3_free(message);
    return 1;
}

static morph_db_status morph_database_migrate_api(
    void *context, const morph_db_migration *migrations,
    unsigned long migration_count, unsigned int target_version)
{
    morph_database_service *service = context;
    unsigned int current = 0;
    unsigned long index;
    if (!morph_database_valid_thread(service)) return MORPH_DB_INVALID;
    if (service->transaction_active) return morph_database_set_error(
        service, SQLITE_MISUSE, "Migrations cannot run inside a transaction");
    if ((migration_count && !migrations) || migration_count > UINT_MAX)
        return morph_database_set_error(
            service, SQLITE_MISUSE, "Migration array is invalid");
    if (!morph_database_read_user_version(service, &current))
        return service->error.status;
    if (current > target_version) {
        service->error.status = MORPH_DB_SCHEMA_NEWER;
        service->error.backend_code = SQLITE_SCHEMA;
        snprintf(service->error.message, sizeof(service->error.message),
            "Database schema version %u is newer than supported version %u",
            current, target_version);
        return MORPH_DB_SCHEMA_NEWER;
    }
    if (current == target_version) {
        morph_database_clear_error(service);
        return MORPH_DB_OK;
    }
    if (morph_database_begin_api(service) != MORPH_DB_OK)
        return service->error.status;
    for (index = 0; index < migration_count && current < target_version; ++index) {
        const morph_db_migration *migration = &migrations[index];
        if (migration->version <= current) continue;
        if (migration->version != current + 1u ||
            migration->version > target_version ||
            !morph_database_run_script(
                service, migration->sql, migration->sql_size) ||
            !morph_database_write_user_version(service, migration->version)) {
            morph_db_error saved_error;
            if (service->error.status == MORPH_DB_OK)
                (void)morph_database_set_error(service, SQLITE_SCHEMA,
                    "Migration versions must be contiguous");
            saved_error = service->error;
            morph_database_rollback_api(service);
            service->error = saved_error;
            return saved_error.status;
        }
        current = migration->version;
    }
    if (current != target_version) {
        morph_db_error saved_error;
        (void)morph_database_set_error(service, SQLITE_SCHEMA,
            "Migration list does not reach the requested schema version");
        saved_error = service->error;
        morph_database_rollback_api(service);
        service->error = saved_error;
        return saved_error.status;
    }
    return morph_database_commit_api(service);
}

static void morph_database_last_error_api(void *context, morph_db_error *error)
{
    morph_database_service *service = context;
    if (!error) return;
    if (!service) {
        memset(error, 0, sizeof(*error));
        error->status = MORPH_DB_INVALID;
        snprintf(error->message, sizeof(error->message), "Database unavailable");
        return;
    }
    *error = service->error;
}

static const morph_database_api database_api = {
    MORPHEUS_DATABASE_ABI_VERSION,
    sizeof(morph_database_api),
    morph_database_execute_api,
    morph_database_prepare_api,
    morph_database_finalize_api,
    morph_database_reset_api,
    morph_database_bind_null_api,
    morph_database_bind_integer_api,
    morph_database_bind_real_api,
    morph_database_bind_text_api,
    morph_database_bind_blob_api,
    morph_database_step_api,
    morph_database_column_count_api,
    morph_database_column_type_api,
    morph_database_column_integer_api,
    morph_database_column_real_api,
    morph_database_column_text_api,
    morph_database_column_blob_api,
    morph_database_last_insert_rowid_api,
    morph_database_changes_api,
    morph_database_begin_api,
    morph_database_commit_api,
    morph_database_rollback_api,
    morph_database_migrate_api,
    morph_database_last_error_api
};

morph_database_service *morph_database_service_create(
    const char *path, char *error, unsigned long error_capacity)
{
    morph_database_service *service;
    int result;
    char *message = NULL;
    if (error && error_capacity) error[0] = '\0';
    if (!path || !*path || strlen(path) >= MORPHEUS_DATABASE_PATH_CAPACITY) {
        if (error && error_capacity) snprintf(error, (size_t)error_capacity,
            "Database path is empty or exceeds the 4095-byte limit");
        return NULL;
    }
    service = calloc(1, sizeof(*service));
    if (!service) return NULL;
    service->owner_thread = pthread_self();
    result = sqlite3_open_v2(path, &service->database,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
        NULL);
    if (result != SQLITE_OK) goto failed;
    sqlite3_extended_result_codes(service->database, 1);
    (void)sqlite3_db_config(service->database, SQLITE_DBCONFIG_DEFENSIVE, 1, NULL);
    sqlite3_limit(service->database, SQLITE_LIMIT_LENGTH,
        (int)MORPHEUS_DATABASE_MAX_VALUE);
    sqlite3_limit(service->database, SQLITE_LIMIT_SQL_LENGTH,
        (int)MORPHEUS_DATABASE_MAX_SQL);
    sqlite3_limit(service->database, SQLITE_LIMIT_COLUMN, 256);
    sqlite3_limit(service->database, SQLITE_LIMIT_VARIABLE_NUMBER, 999);
    sqlite3_busy_timeout(service->database, 250);
    sqlite3_progress_handler(service->database, 10000,
        morph_database_progress, service);
    sqlite3_set_authorizer(service->database, morph_database_authorize, service);
    service->allow_sensitive_pragma = 1;
    result = sqlite3_exec(service->database,
        "PRAGMA foreign_keys = ON;"
        "PRAGMA journal_mode = WAL;"
        "PRAGMA synchronous = NORMAL;"
        "PRAGMA trusted_schema = OFF;"
        "PRAGMA recursive_triggers = ON;",
        NULL, NULL, &message);
    service->allow_sensitive_pragma = 0;
    if (result != SQLITE_OK) goto failed;
    sqlite3_free(message);
    snprintf(service->accepted_path, sizeof(service->accepted_path), "%s", path);
    snprintf(service->active_path, sizeof(service->active_path), "%s", path);
    morph_database_clear_error(service);
    return service;

failed:
    if (error && error_capacity) snprintf(error, (size_t)error_capacity, "%s",
        message && *message ? message :
        service->database ? sqlite3_errmsg(service->database) :
        "Unable to open application database");
    sqlite3_free(message);
    if (service->database) sqlite3_close(service->database);
    free(service);
    return NULL;
}

static void morph_database_service_close_connection(
    morph_database_service *service)
{
    unsigned int index;
    if (!service || !service->database) return;
    if (service->transaction_active) morph_database_rollback_api(service);
    for (index = 0; index < MORPHEUS_DATABASE_MAX_STATEMENTS; ++index) {
        morph_database_statement_slot *slot = &service->statements[index];
        if (slot->statement) sqlite3_finalize(slot->statement);
        slot->statement = NULL;
        slot->generation++;
        if (!slot->generation) slot->generation++;
    }
    sqlite3_close(service->database);
    service->database = NULL;
    service->transaction_active = 0;
}

static void morph_database_service_adopt(
    morph_database_service *service, morph_database_service *replacement)
{
    morph_database_service_close_connection(service);
    service->database = replacement->database;
    replacement->database = NULL;
    sqlite3_progress_handler(service->database, 10000,
        morph_database_progress, service);
    sqlite3_set_authorizer(service->database,
        morph_database_authorize, service);
    snprintf(service->active_path, sizeof(service->active_path), "%s",
        replacement->active_path);
    morph_database_clear_error(service);
    free(replacement);
}

static int morph_database_service_backup(
    morph_database_service *destination,
    morph_database_service *source,
    char *error,
    unsigned long error_capacity)
{
    sqlite3_backup *backup;
    int result;
    if (error && error_capacity) error[0] = '\0';
    backup = sqlite3_backup_init(
        destination->database, "main", source->database, "main");
    if (!backup) {
        result = sqlite3_errcode(destination->database);
    } else {
        result = sqlite3_backup_step(backup, -1);
        if (result == SQLITE_DONE) result = SQLITE_OK;
        if (sqlite3_backup_finish(backup) != SQLITE_OK && result == SQLITE_OK)
            result = sqlite3_errcode(destination->database);
    }
    if (result == SQLITE_OK) return 1;
    if (error && error_capacity) snprintf(error, (size_t)error_capacity, "%s",
        sqlite3_errmsg(destination->database));
    return 0;
}

int morph_database_service_switch_path(
    morph_database_service *service, const char *path,
    char *error, unsigned long error_capacity)
{
    morph_database_service *replacement;
    if (!service || !path || !*path || service->preview_active) return 0;
    if (service->transaction_active) morph_database_rollback_api(service);
    if (strcmp(service->accepted_path, path) == 0) return 1;
    replacement = morph_database_service_create(path, error, error_capacity);
    if (!replacement) return 0;
    morph_database_service_adopt(service, replacement);
    snprintf(service->accepted_path, sizeof(service->accepted_path), "%s", path);
    return 1;
}

int morph_database_service_begin_preview(
    morph_database_service *service, char *error, unsigned long error_capacity)
{
    morph_database_service *preview;
    char preview_path[MORPHEUS_DATABASE_PATH_CAPACITY];
    int length;
    if (!service || service->preview_active || service->transaction_active ||
        strcmp(service->accepted_path, ":memory:") == 0) return 0;
    length = snprintf(preview_path, sizeof(preview_path), "%s.preview",
        service->accepted_path);
    if (length < 0 || (unsigned long)length >= sizeof(preview_path)) return 0;
    preview = morph_database_service_create(preview_path, error, error_capacity);
    if (!preview) return 0;
    if (!morph_database_service_backup(preview, service, error, error_capacity)) {
        morph_database_service_destroy(preview);
        return 0;
    }
    morph_database_service_adopt(service, preview);
    service->preview_active = 1;
    return 1;
}

int morph_database_service_accept_preview(
    morph_database_service *service, char *error, unsigned long error_capacity)
{
    morph_database_service *accepted;
    if (!service || !service->preview_active || service->transaction_active) return 0;
    accepted = morph_database_service_create(
        service->accepted_path, error, error_capacity);
    if (!accepted) return 0;
    if (!morph_database_service_backup(accepted, service, error, error_capacity)) {
        morph_database_service_destroy(accepted);
        return 0;
    }
    morph_database_service_adopt(service, accepted);
    service->preview_active = 0;
    return 1;
}

int morph_database_service_reject_preview(
    morph_database_service *service, char *error, unsigned long error_capacity)
{
    morph_database_service *accepted;
    if (!service || !service->preview_active) return 0;
    if (service->transaction_active) morph_database_rollback_api(service);
    accepted = morph_database_service_create(
        service->accepted_path, error, error_capacity);
    if (!accepted) return 0;
    morph_database_service_adopt(service, accepted);
    service->preview_active = 0;
    return 1;
}

int morph_database_service_is_previewing(const morph_database_service *service)
{
    return service && service->preview_active;
}

void morph_database_service_destroy(morph_database_service *service)
{
    if (!service) return;
    morph_database_service_close_connection(service);
    free(service);
}

morph_capability morph_database_service_capability(
    morph_database_service *service)
{
    morph_capability capability = {
        MORPHEUS_DATABASE_CAPABILITY,
        MORPHEUS_DATABASE_ABI_VERSION,
        sizeof(morph_database_api),
        &database_api,
        service
    };
    return capability;
}
