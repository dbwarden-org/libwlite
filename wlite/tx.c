/*
 * tx.c — Transaction API with savepoints
 */

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sqlite3.h>
#include "wlite/wlite.h"
#include "internal.h"

struct wlite_tx {
    sqlite3 *sqlite;
    int committed;
};

wlite_result wlite_begin(wlite_db *db, wlite_tx **out) {
    if (!db || !out) return WLITE_INVALID_ARGUMENT;
    char *err = NULL;
    int rc = sqlite3_exec(db->sqlite, "BEGIN IMMEDIATE", NULL, NULL, &err);
    if (rc != SQLITE_OK) { sqlite3_free(err); return WLITE_SQLITE_ERROR; }
    *out = calloc(1, sizeof(wlite_tx));
    if (!*out) { sqlite3_exec(db->sqlite, "ROLLBACK", NULL, NULL, NULL); return WLITE_OUT_OF_MEMORY; }
    (*out)->sqlite = db->sqlite;
    return WLITE_OK;
}

wlite_result wlite_commit(wlite_tx *tx) {
    if (!tx || tx->committed) return WLITE_INVALID_ARGUMENT;
    char *err = NULL;
    int rc = sqlite3_exec(tx->sqlite, "COMMIT", NULL, NULL, &err);
    if (rc != SQLITE_OK) { sqlite3_free(err); free(tx); return WLITE_SQLITE_ERROR; }
    tx->committed = 1;
    free(tx);
    return WLITE_OK;
}

wlite_result wlite_rollback(wlite_tx *tx) {
    if (!tx) return WLITE_INVALID_ARGUMENT;
    if (!tx->committed) {
        char *err = NULL;
        sqlite3_exec(tx->sqlite, "ROLLBACK", NULL, NULL, &err);
        sqlite3_free(err);
    }
    free(tx);
    return WLITE_OK;
}

void wlite_tx_free(wlite_tx *tx) {
    if (!tx) return;
    if (!tx->committed) {
        char *err = NULL;
        sqlite3_exec(tx->sqlite, "ROLLBACK", NULL, NULL, &err);
        sqlite3_free(err);
    }
    free(tx);
}

/* ── Savepoints ──────────────────────────────────────────────────────── */

wlite_result wlite_savepoint(wlite_tx *tx, const char *name) {
    if (!tx || !name || tx->committed) return WLITE_INVALID_ARGUMENT;
    char sql[256];
    snprintf(sql, sizeof(sql), "SAVEPOINT %s", name);
    char *err = NULL;
    int rc = sqlite3_exec(tx->sqlite, sql, NULL, NULL, &err);
    if (rc != SQLITE_OK) { sqlite3_free(err); return WLITE_SQLITE_ERROR; }
    return WLITE_OK;
}

wlite_result wlite_release(wlite_tx *tx, const char *name) {
    if (!tx || !name || tx->committed) return WLITE_INVALID_ARGUMENT;
    char sql[256];
    snprintf(sql, sizeof(sql), "RELEASE SAVEPOINT %s", name);
    char *err = NULL;
    int rc = sqlite3_exec(tx->sqlite, sql, NULL, NULL, &err);
    if (rc != SQLITE_OK) { sqlite3_free(err); return WLITE_SQLITE_ERROR; }
    return WLITE_OK;
}

wlite_result wlite_rollback_to(wlite_tx *tx, const char *name) {
    if (!tx || !name || tx->committed) return WLITE_INVALID_ARGUMENT;
    char sql[256];
    snprintf(sql, sizeof(sql), "ROLLBACK TO SAVEPOINT %s", name);
    char *err = NULL;
    int rc = sqlite3_exec(tx->sqlite, sql, NULL, NULL, &err);
    if (rc != SQLITE_OK) { sqlite3_free(err); return WLITE_SQLITE_ERROR; }
    return WLITE_OK;
}
