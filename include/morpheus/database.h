#ifndef MORPHEUS_DATABASE_H
#define MORPHEUS_DATABASE_H

#include "morpheus/app_api.h"

#define MORPHEUS_DATABASE_CAPABILITY "dev.morpheus.runtime.database"
#define MORPHEUS_DATABASE_ABI_VERSION 1u

typedef unsigned long long morph_db_statement;

typedef enum morph_db_status {
    MORPH_DB_OK = 0,
    MORPH_DB_ROW = 1,
    MORPH_DB_DONE = 2,
    MORPH_DB_BUSY = 3,
    MORPH_DB_CONSTRAINT = 4,
    MORPH_DB_TOO_LARGE = 5,
    MORPH_DB_INTERRUPTED = 6,
    MORPH_DB_SCHEMA_NEWER = 7,
    MORPH_DB_INVALID = 8,
    MORPH_DB_ERROR = 9
} morph_db_status;

typedef enum morph_db_value_type {
    MORPH_DB_NULL = 0,
    MORPH_DB_INTEGER = 1,
    MORPH_DB_REAL = 2,
    MORPH_DB_TEXT = 3,
    MORPH_DB_BLOB = 4
} morph_db_value_type;

typedef struct morph_db_bytes {
    const void *data;
    unsigned long size;
} morph_db_bytes;

typedef struct morph_db_error {
    morph_db_status status;
    int backend_code;
    char message[192];
} morph_db_error;

/* Versions must be contiguous, begin at 1, and end at target_version. */
typedef struct morph_db_migration {
    unsigned int version;
    const char *sql;
    unsigned long sql_size;
} morph_db_migration;

typedef struct morph_database_api {
    unsigned int abi_version;
    unsigned long struct_size;

    /* execute and prepare accept exactly one SQL statement. */
    morph_db_status (*execute)(
        void *context, const char *sql, unsigned long sql_size);
    morph_db_status (*prepare)(
        void *context, const char *sql, unsigned long sql_size,
        morph_db_statement *statement);
    void (*finalize)(void *context, morph_db_statement statement);
    morph_db_status (*reset)(
        void *context, morph_db_statement statement, int clear_bindings);

    /* Text and blob bindings are copied before the call returns. */
    morph_db_status (*bind_null)(
        void *context, morph_db_statement statement, unsigned int index);
    morph_db_status (*bind_integer)(
        void *context, morph_db_statement statement, unsigned int index,
        long long value);
    morph_db_status (*bind_real)(
        void *context, morph_db_statement statement, unsigned int index,
        double value);
    morph_db_status (*bind_text)(
        void *context, morph_db_statement statement, unsigned int index,
        const char *value, unsigned long size);
    morph_db_status (*bind_blob)(
        void *context, morph_db_statement statement, unsigned int index,
        const void *value, unsigned long size);

    morph_db_status (*step)(
        void *context, morph_db_statement statement);
    unsigned int (*column_count)(
        void *context, morph_db_statement statement);
    morph_db_value_type (*column_type)(
        void *context, morph_db_statement statement, unsigned int column);
    long long (*column_integer)(
        void *context, morph_db_statement statement, unsigned int column);
    double (*column_real)(
        void *context, morph_db_statement statement, unsigned int column);
    /* Returned bytes are borrowed until step, reset, or finalize. */
    morph_db_bytes (*column_text)(
        void *context, morph_db_statement statement, unsigned int column);
    morph_db_bytes (*column_blob)(
        void *context, morph_db_statement statement, unsigned int column);

    long long (*last_insert_rowid)(void *context);
    unsigned long (*changes)(void *context);
    morph_db_status (*begin)(void *context);
    morph_db_status (*commit)(void *context);
    void (*rollback)(void *context);
    morph_db_status (*migrate)(
        void *context, const morph_db_migration *migrations,
        unsigned long migration_count, unsigned int target_version);
    void (*last_error)(void *context, morph_db_error *error);
} morph_database_api;

static inline const morph_database_api *
morph_database_from_capability(const morph_capability *capability)
{
    const morph_database_api *api;
    if (!capability ||
        !capability->identifier ||
        strcmp(capability->identifier, MORPHEUS_DATABASE_CAPABILITY) != 0 ||
        capability->abi_version < MORPHEUS_DATABASE_ABI_VERSION ||
        capability->api_size < sizeof(morph_database_api) ||
        !capability->api) return NULL;
    api = (const morph_database_api *)capability->api;
    if (api->abi_version < MORPHEUS_DATABASE_ABI_VERSION ||
        api->struct_size < sizeof(morph_database_api)) return NULL;
    return api;
}

#endif
