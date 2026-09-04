/*
 * migrate.c — Migration execution, schema hashing, verification
 */

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sqlite3.h>
#include "wlite/wlite.h"
#include "internal.h"

/* ── Schema hash (FNV-1a) ────────────────────────────────────────────── */

static uint64_t fnv1a(const char *data, size_t len) {
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < len; i++) { h ^= (uint8_t)data[i]; h *= 1099511628211ULL; }
    return h;
}

static void hash_str(uint64_t *h, const char *s) {
    if (!s) return;
    *h = fnv1a(s, strlen(s)) ^ (*h << 7);
}

static void hash_int(uint64_t *h, int v) {
    char buf[32]; int n = snprintf(buf, sizeof(buf), "%d", v);
    *h = fnv1a(buf, n) ^ (*h << 3);
}

char *wl_schema_hash(const WlSchema *schema) {
    if (!schema) return NULL;
    uint64_t h = 14695981039346656037ULL;
    hash_int(&h, schema->version);
    hash_int(&h, (int)schema->table_count);
    for (size_t i = 0; i < schema->table_count; i++) {
        WlTable *t = &schema->tables[i];
        hash_str(&h, t->name);
        hash_int(&h, (int)t->column_count);
        hash_int(&h, t->strict);
        hash_int(&h, t->without_rowid);
        for (size_t j = 0; j < t->column_count; j++) {
            WlColumn *c = &t->columns[j];
            hash_str(&h, c->name); hash_str(&h, c->type_name);
            hash_int(&h, c->not_null); hash_int(&h, c->primary_key);
            hash_int(&h, c->is_unique); hash_int(&h, c->autoincrement);
            hash_str(&h, c->default_expr); hash_str(&h, c->collate);
            hash_str(&h, c->fk_table); hash_str(&h, c->fk_column);
        }
    }
    hash_int(&h, (int)schema->index_count);
    for (size_t i = 0; i < schema->index_count; i++) {
        hash_str(&h, schema->indexes[i].name);
        hash_str(&h, schema->indexes[i].table);
    }
    hash_int(&h, (int)schema->view_count);
    hash_int(&h, (int)schema->trigger_count);
    char *hex = malloc(17);
    snprintf(hex, 17, "%016llx", (unsigned long long)h);
    return hex;
}

/* ── Migration tracking ──────────────────────────────────────────────── */

static int ensure_migration_table(sqlite3 *db) {
    return sqlite3_exec(db,
        "CREATE TABLE IF NOT EXISTS _wlite_migrations ("
        "id INTEGER PRIMARY KEY, name TEXT NOT NULL, "
        "checksum TEXT NOT NULL, applied_at INTEGER NOT NULL);",
        NULL, NULL, NULL);
}

static int is_applied(sqlite3 *db, const char *checksum) {
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, "SELECT 1 FROM _wlite_migrations WHERE checksum=? LIMIT 1", -1, &stmt, NULL);
    if (rc != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt, 1, checksum, -1, SQLITE_STATIC);
    int found = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return found;
}

static int record_migration(sqlite3 *db, int id, const char *name, const char *checksum) {
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db,
        "INSERT INTO _wlite_migrations (id,name,checksum,applied_at) VALUES (?,?,?,strftime('%s','now'))",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return rc;
    sqlite3_bind_int(stmt, 1, id);
    sqlite3_bind_text(stmt, 2, name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, checksum, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt); sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? SQLITE_OK : rc;
}

/* ── Execute SQL ─────────────────────────────────────────────────────── */

static int exec_sql(sqlite3 *db, const char *sql, wlite_error **err) {
    char *errmsg = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        if (err) { *err = calloc(1, sizeof(wlite_error)); (*err)->code = WLITE_SQLITE_ERROR;
            (*err)->message = errmsg ? strdup(errmsg) : strdup("unknown error"); (*err)->sqlite_code = rc; }
        sqlite3_free(errmsg);
    }
    return rc;
}

/* ── Apply plan ──────────────────────────────────────────────────────── */

wlite_result wl_apply_plan(wlite_db *db, const WlPlan *plan, wlite_error **error) {
    if (!db || !plan) { if (error) { *error = calloc(1, sizeof(wlite_error));
        (*error)->code = WLITE_INVALID_ARGUMENT; (*error)->message = strdup("NULL pointer"); }
        return WLITE_INVALID_ARGUMENT; }
    sqlite3_busy_timeout(db->sqlite, 5000);
    for (size_t i = 0; i < plan->step_count; i++) {
        if (!plan->steps[i].sql) continue;
        if (plan->steps[i].is_non_atomic) {
            int rc = exec_sql(db->sqlite, "BEGIN IMMEDIATE;", error);
            if (rc != SQLITE_OK) return WLITE_SQLITE_ERROR;
            rc = exec_sql(db->sqlite, plan->steps[i].sql, error);
            if (rc != SQLITE_OK) { exec_sql(db->sqlite, "ROLLBACK;", NULL); return WLITE_SQLITE_ERROR; }
            rc = exec_sql(db->sqlite, "COMMIT;", error);
            if (rc != SQLITE_OK) return WLITE_SQLITE_ERROR;
        } else {
            int rc = exec_sql(db->sqlite, plan->steps[i].sql, error);
            if (rc != SQLITE_OK) return WLITE_SQLITE_ERROR;
        }
    }
    return WLITE_OK;
}

/* ── Rollback ────────────────────────────────────────────────────────── */

wlite_result wl_rollback_last(wlite_db *db, wlite_error **error) {
    if (!db) { if (error) { *error = calloc(1, sizeof(wlite_error));
        (*error)->code = WLITE_INVALID_ARGUMENT; (*error)->message = strdup("NULL pointer"); }
        return WLITE_INVALID_ARGUMENT; }
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db->sqlite, "SELECT id FROM _wlite_migrations ORDER BY id DESC LIMIT 1", -1, &stmt, NULL);
    if (rc != SQLITE_OK) return WLITE_SQLITE_ERROR;
    if (sqlite3_step(stmt) != SQLITE_ROW) { sqlite3_finalize(stmt); return WLITE_OK; }
    int id = sqlite3_column_int(stmt, 0); sqlite3_finalize(stmt);
    rc = sqlite3_prepare_v2(db->sqlite, "DELETE FROM _wlite_migrations WHERE id=?", -1, &stmt, NULL);
    if (rc != SQLITE_OK) return WLITE_SQLITE_ERROR;
    sqlite3_bind_int(stmt, 1, id);
    rc = sqlite3_step(stmt); sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? WLITE_OK : WLITE_SQLITE_ERROR;
}

/* ── Single-call diff ────────────────────────────────────────────────── */

wlite_result wlite_diff(wlite_db *db, const wlite_model *model, WlPlan **out_plan) {
    if (!db || !model || !model->schema || !out_plan) return WLITE_INVALID_ARGUMENT;
    WlSchema *current = wl_schema_introspect(db->sqlite, NULL);
    if (!current) { current = calloc(1, sizeof(WlSchema)); }
    *out_plan = wl_plan_migration(current, model->schema, NULL);
    wl_schema_free(current);
    return *out_plan ? WLITE_OK : WLITE_ERROR;
}

/* ── Plan inspection ─────────────────────────────────────────────────── */

size_t wlite_plan_count(const WlPlan *plan) {
    return plan ? plan->step_count : 0;
}

/* ── Verify ──────────────────────────────────────────────────────────── */

wlite_result wl_schema_verify(wlite_db *db, const WlSchema *expected, WlDiff **difference, wlite_error **error) {
    WlSchema *actual = wl_schema_introspect(db->sqlite, error);
    if (!actual) return WLITE_ERROR;
    WlDiff *diff = wl_schema_diff(actual, expected, error);
    wl_schema_free(actual);
    if (!diff) return WLITE_ERROR;
    if (diff->entry_count == 0) { wl_diff_free(diff); if (difference) *difference = NULL; return WLITE_OK; }
    if (difference) *difference = diff; else wl_diff_free(diff);
    return WLITE_NOT_FOUND;
}
