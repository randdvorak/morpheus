#include <stdio.h>
#include <string.h>

#include "sqlite3.h"

static int fail(sqlite3 *database, const char *stage, int result)
{
    fprintf(stderr, "%s failed (%d): %s\n",
        stage,
        result,
        database ? sqlite3_errmsg(database) : "database unavailable");
    if (database) sqlite3_close(database);
    return 1;
}

int main(void)
{
    sqlite3 *database = NULL;
    sqlite3_stmt *statement = NULL;
    const char *name;
    int result;

    result = sqlite3_open_v2(
        ":memory:",
        &database,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX,
        NULL);
    if (result != SQLITE_OK) return fail(database, "open", result);

    result = sqlite3_exec(
        database,
        "PRAGMA foreign_keys = ON;"
        "CREATE TABLE app_state (key TEXT PRIMARY KEY, value TEXT NOT NULL);"
        "BEGIN IMMEDIATE;"
        "INSERT INTO app_state(key, value) VALUES('name', 'Morpheus');"
        "COMMIT;",
        NULL,
        NULL,
        NULL);
    if (result != SQLITE_OK) return fail(database, "schema/transaction", result);

    result = sqlite3_prepare_v2(
        database,
        "SELECT value FROM app_state WHERE key = ?1",
        -1,
        &statement,
        NULL);
    if (result != SQLITE_OK) return fail(database, "prepare", result);
    result = sqlite3_bind_text(statement, 1, "name", -1, SQLITE_STATIC);
    if (result != SQLITE_OK) {
        sqlite3_finalize(statement);
        return fail(database, "bind", result);
    }
    result = sqlite3_step(statement);
    if (result != SQLITE_ROW) {
        sqlite3_finalize(statement);
        return fail(database, "step", result);
    }
    name = (const char *)sqlite3_column_text(statement, 0);
    if (!name || strcmp(name, "Morpheus") != 0) {
        sqlite3_finalize(statement);
        return fail(database, "value verification", SQLITE_MISMATCH);
    }
    if (sqlite3_finalize(statement) != SQLITE_OK) {
        return fail(database, "finalize", sqlite3_errcode(database));
    }
    if (sqlite3_close(database) != SQLITE_OK) {
        return fail(database, "close", sqlite3_errcode(database));
    }
    puts("PASS: embedded SQLite schema, transaction, binding, and query");
    return 0;
}
