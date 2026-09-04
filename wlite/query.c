/*
 * query.c — Prepared statement API
 */

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include "wlite/wlite.h"
#include "internal.h"

struct wlite_stmt {
    sqlite3_stmt *stmt;
};

wlite_result wlite_prepare(wlite_db *db, const char *sql, wlite_stmt **out) {
    if (!db || !sql || !out) return WLITE_INVALID_ARGUMENT;
    *out = calloc(1, sizeof(wlite_stmt));
    if (!*out) return WLITE_OUT_OF_MEMORY;
    int rc = sqlite3_prepare_v2(db->sqlite, sql, -1, &(*out)->stmt, NULL);
    if (rc != SQLITE_OK) { free(*out); *out = NULL; return WLITE_SQLITE_ERROR; }
    return WLITE_OK;
}

wlite_result wlite_bind_null(wlite_stmt *stmt, int index) {
    if (!stmt) return WLITE_INVALID_ARGUMENT;
    return sqlite3_bind_null(stmt->stmt, index) == SQLITE_OK ? WLITE_OK : WLITE_SQLITE_ERROR;
}

wlite_result wlite_bind_int64(wlite_stmt *stmt, int index, int64_t value) {
    if (!stmt) return WLITE_INVALID_ARGUMENT;
    return sqlite3_bind_int64(stmt->stmt, index, value) == SQLITE_OK ? WLITE_OK : WLITE_SQLITE_ERROR;
}

wlite_result wlite_bind_double(wlite_stmt *stmt, int index, double value) {
    if (!stmt) return WLITE_INVALID_ARGUMENT;
    return sqlite3_bind_double(stmt->stmt, index, value) == SQLITE_OK ? WLITE_OK : WLITE_SQLITE_ERROR;
}

wlite_result wlite_bind_text(wlite_stmt *stmt, int index, const char *value) {
    if (!stmt) return WLITE_INVALID_ARGUMENT;
    return sqlite3_bind_text(stmt->stmt, index, value, -1, SQLITE_TRANSIENT) == SQLITE_OK ? WLITE_OK : WLITE_SQLITE_ERROR;
}

wlite_result wlite_bind_text_n(wlite_stmt *stmt, int index, const char *value, size_t length) {
    if (!stmt) return WLITE_INVALID_ARGUMENT;
    return sqlite3_bind_text(stmt->stmt, index, value, (int)length, SQLITE_TRANSIENT) == SQLITE_OK ? WLITE_OK : WLITE_SQLITE_ERROR;
}

wlite_result wlite_bind_blob(wlite_stmt *stmt, int index, const void *data, size_t size) {
    if (!stmt) return WLITE_INVALID_ARGUMENT;
    return sqlite3_bind_blob(stmt->stmt, index, data, (int)size, SQLITE_TRANSIENT) == SQLITE_OK ? WLITE_OK : WLITE_SQLITE_ERROR;
}

wlite_result wlite_step(wlite_stmt *stmt) {
    if (!stmt) return WLITE_INVALID_ARGUMENT;
    int rc = sqlite3_step(stmt->stmt);
    if (rc == SQLITE_ROW) return WLITE_OK;
    if (rc == SQLITE_DONE) {
        sqlite3_reset(stmt->stmt);
        return WLITE_NOT_FOUND;
    }
    return WLITE_SQLITE_ERROR;
}

void wlite_stmt_reset(wlite_stmt *stmt) {
    if (stmt) sqlite3_reset(stmt->stmt);
}

void wlite_stmt_finalize(wlite_stmt *stmt) {
    if (!stmt) return;
    sqlite3_finalize(stmt->stmt);
    free(stmt);
}

int wlite_column_count(wlite_stmt *stmt) {
    return stmt ? sqlite3_column_count(stmt->stmt) : 0;
}

const char *wlite_column_name(wlite_stmt *stmt, int column) {
    return stmt ? sqlite3_column_name(stmt->stmt, column) : NULL;
}

wlite_value_type wlite_column_type(wlite_stmt *stmt, int column) {
    if (!stmt) return WLITE_TYPE_NULL;
    switch (sqlite3_column_type(stmt->stmt, column)) {
        case SQLITE_INTEGER: return WLITE_TYPE_INTEGER;
        case SQLITE_FLOAT:   return WLITE_TYPE_REAL;
        case SQLITE_TEXT:    return WLITE_TYPE_TEXT;
        case SQLITE_BLOB:   return WLITE_TYPE_BLOB;
        default:            return WLITE_TYPE_NULL;
    }
}

int64_t wlite_column_int64(wlite_stmt *stmt, int column) {
    return stmt ? sqlite3_column_int64(stmt->stmt, column) : 0;
}

double wlite_column_double(wlite_stmt *stmt, int column) {
    return stmt ? sqlite3_column_double(stmt->stmt, column) : 0.0;
}

const char *wlite_column_text(wlite_stmt *stmt, int column) {
    return stmt ? (const char *)sqlite3_column_text(stmt->stmt, column) : NULL;
}

const void *wlite_column_blob(wlite_stmt *stmt, int column) {
    return stmt ? sqlite3_column_blob(stmt->stmt, column) : NULL;
}

size_t wlite_column_bytes(wlite_stmt *stmt, int column) {
    return stmt ? (size_t)sqlite3_column_bytes(stmt->stmt, column) : 0;
}

/* ── Convenience SQL execution ───────────────────────────────────────── */

wlite_result wlite_execute(wlite_db *db, const char *sql, int64_t *rows_affected) {
    if (!db || !sql) return WLITE_INVALID_ARGUMENT;
    char *errmsg = NULL;
    int rc = sqlite3_exec(db->sqlite, sql, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) { sqlite3_free(errmsg); return WLITE_SQLITE_ERROR; }
    if (rows_affected) *rows_affected = (int64_t)sqlite3_changes(db->sqlite);
    return WLITE_OK;
}
