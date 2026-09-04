/*
 * schema.c — Schema lifecycle, database API, model API, memory management
 */

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sqlite3.h>
#include "wlite/wlite.h"
#include "internal.h"

/* ── ABI version ─────────────────────────────────────────────────────── */

int wlite_abi_version(void) { return WLITE_ABI_VERSION; }

const char *wlite_version(void) {
    return "0.2.0";
}

/* ── Error string lookup ─────────────────────────────────────────────── */

const char *wlite_strerror(wlite_result result) {
    switch (result) {
        case WLITE_OK: return "OK";
        case WLITE_ERROR: return "general error";
        case WLITE_INVALID_ARGUMENT: return "invalid argument";
        case WLITE_OUT_OF_MEMORY: return "out of memory";
        case WLITE_IO_ERROR: return "I/O error";
        case WLITE_PARSE_ERROR: return "parse error";
        case WLITE_MODEL_ERROR: return "model error";
        case WLITE_SQLITE_ERROR: return "SQLite error";
        case WLITE_CONSTRAINT_ERROR: return "constraint violation";
        case WLITE_NOT_FOUND: return "not found";
        case WLITE_BUSY: return "busy";
        case WLITE_TRANSACTION_ERROR: return "transaction error";
        default: return "unknown error";
    }
}

/* ── Memory helpers ──────────────────────────────────────────────────── */

char *wlite_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *dup = malloc(len);
    if (dup) memcpy(dup, s, len);
    return dup;
}

void wlite_free(void *p) { free(p); }

/* ── Schema lifecycle ────────────────────────────────────────────────── */

static void wl_column_free(WlColumn *c) {
    if (!c) return;
    free(c->name); free(c->type_name); free(c->default_expr);
    free(c->collate); free(c->generated_expr);
    free(c->fk_table); free(c->fk_column);
    free(c->fk_on_delete); free(c->fk_on_update);
}

static void wl_foreign_key_free(WlForeignKey *fk) {
    if (!fk) return;
    for (size_t i = 0; i < fk->column_count; i++) free(fk->columns[i]);
    free(fk->columns); free(fk->ref_table);
    for (size_t i = 0; i < fk->ref_column_count; i++) free(fk->ref_columns[i]);
    free(fk->ref_columns);
}

static void wl_table_free(WlTable *t) {
    if (!t) return;
    free(t->name); free(t->comment);
    for (size_t i = 0; i < t->column_count; i++) wl_column_free(&t->columns[i]);
    free(t->columns);
    for (size_t i = 0; i < t->primary_key.column_count; i++) free(t->primary_key.columns[i]);
    free(t->primary_key.columns);
    for (size_t i = 0; i < t->foreign_key_count; i++) wl_foreign_key_free(&t->foreign_keys[i]);
    free(t->foreign_keys);
    for (size_t i = 0; i < t->check_count; i++) free(t->checks[i].expression);
    free(t->checks);
    for (size_t i = 0; i < t->unique_count; i++) {
        for (size_t j = 0; j < t->uniques[i].column_count; j++) free(t->uniques[i].columns[j]);
        free(t->uniques[i].columns);
    }
    free(t->uniques);
}

static void wl_index_free(WlIndex *idx) {
    if (!idx) return;
    free(idx->name); free(idx->table);
    for (size_t i = 0; i < idx->column_count; i++) free(idx->columns[i]);
    free(idx->columns); free(idx->expression); free(idx->where_clause);
}

void wl_schema_free(WlSchema *schema) {
    if (!schema) return;
    for (size_t i = 0; i < schema->table_count; i++) wl_table_free(&schema->tables[i]);
    free(schema->tables);
    for (size_t i = 0; i < schema->index_count; i++) wl_index_free(&schema->indexes[i]);
    free(schema->indexes);
    for (size_t i = 0; i < schema->view_count; i++) { free(schema->views[i].name); free(schema->views[i].sql); }
    free(schema->views);
    for (size_t i = 0; i < schema->trigger_count; i++) {
        free(schema->triggers[i].name); free(schema->triggers[i].table); free(schema->triggers[i].sql);
    }
    free(schema->triggers);
    free(schema->model_name);
    free(schema);
}

/* ── Model identity ──────────────────────────────────────────────────── */

const char *wl_schema_model_name(const WlSchema *schema) {
    return schema ? schema->model_name : NULL;
}

int wl_schema_model_version(const WlSchema *schema) {
    return schema ? schema->model_version : 0;
}

/* ── Error lifecycle ─────────────────────────────────────────────────── */

void wlite_error_free(wlite_error *err) {
    if (!err) return;
    free(err->message); free(err->subsystem); free(err->object);
    free(err);
}

/* ── Database API ────────────────────────────────────────────────────── */

wlite_result wlite_open(const char *path, wlite_db **out) {
    wlite_open_options opts = { .create = 1, .foreign_keys = 1, .busy_timeout_ms = 5000 };
    return wlite_open_ex(path, &opts, out);
}

wlite_result wlite_open_ex(const char *path, const wlite_open_options *options, wlite_db **out) {
    if (!path || !out) return WLITE_INVALID_ARGUMENT;
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    if (options) {
        if (options->readonly) flags = SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX;
    }
    *out = calloc(1, sizeof(wlite_db));
    if (!*out) return WLITE_OUT_OF_MEMORY;
    int rc = sqlite3_open_v2(path, &(*out)->sqlite, flags, NULL);
    if (rc != SQLITE_OK) { free(*out); *out = NULL; return WLITE_SQLITE_ERROR; }
    if (options && options->foreign_keys) {
        sqlite3_exec((*out)->sqlite, "PRAGMA foreign_keys = ON", NULL, NULL, NULL);
    }
    if (options && options->busy_timeout_ms > 0) {
        sqlite3_busy_timeout((*out)->sqlite, options->busy_timeout_ms);
    }
    return WLITE_OK;
}

void wlite_close(wlite_db *db) {
    if (db) { sqlite3_close(db->sqlite); free(db); }
}

/* ── Model API ───────────────────────────────────────────────────────── */

wlite_result wlite_model_load_file(const char *path, wlite_model **out) {
    if (!path || !out) return WLITE_INVALID_ARGUMENT;
    wlite_error *err = NULL;
    WlSchema *schema = wl_schema_load(path, &err);
    if (!schema) { wlite_error_free(err); return WLITE_PARSE_ERROR; }
    *out = calloc(1, sizeof(wlite_model));
    if (!*out) { wl_schema_free(schema); return WLITE_OUT_OF_MEMORY; }
    (*out)->schema = schema;
    return WLITE_OK;
}

wlite_result wlite_model_load_memory(const void *data, size_t size, wlite_model **out) {
    if (!data || !out) return WLITE_INVALID_ARGUMENT;
    wlite_error *err = NULL;
    WlSchema *schema = wl_schema_parse((const char *)data, size, &err);
    if (!schema) { wlite_error_free(err); return WLITE_PARSE_ERROR; }
    *out = calloc(1, sizeof(wlite_model));
    if (!*out) { wl_schema_free(schema); return WLITE_OUT_OF_MEMORY; }
    (*out)->schema = schema;
    return WLITE_OK;
}

wlite_result wlite_model_load_compiled(const void *data, size_t size, wlite_model **out) {
    if (!data || !out) return WLITE_INVALID_ARGUMENT;
    WlSchema *schema = wl_model_load_compiled_raw(data, size);
    if (!schema) return WLITE_PARSE_ERROR;
    *out = calloc(1, sizeof(wlite_model));
    if (!*out) { wl_schema_free(schema); return WLITE_OUT_OF_MEMORY; }
    (*out)->schema = schema;
    return WLITE_OK;
}

void wlite_model_free(wlite_model *model) {
    if (!model) return;
    wl_schema_free(model->schema);
    free(model);
}

/* ── Model validation ────────────────────────────────────────────────── */

wlite_result wlite_model_validate(const wlite_model *model) {
    if (!model || !model->schema) return WLITE_INVALID_ARGUMENT;
    WlSchema *s = model->schema;
    for (size_t i = 0; i < s->table_count; i++) {
        if (!s->tables[i].name) return WLITE_MODEL_ERROR;
        for (size_t j = 0; j < s->tables[i].column_count; j++) {
            if (!s->tables[i].columns[j].name) return WLITE_MODEL_ERROR;
            if (!s->tables[i].columns[j].type_name) return WLITE_MODEL_ERROR;
        }
    }
    return WLITE_OK;
}

/* ── Model introspection ─────────────────────────────────────────────── */

size_t wlite_model_table_count(const wlite_model *model) {
    return model && model->schema ? model->schema->table_count : 0;
}

const wlite_table *wlite_model_table_at(const wlite_model *model, size_t index) {
    if (!model || !model->schema || index >= model->schema->table_count) return NULL;
    return &model->schema->tables[index];
}

const wlite_table *wlite_model_table(const wlite_model *model, const char *name) {
    if (!model || !model->schema || !name) return NULL;
    for (size_t i = 0; i < model->schema->table_count; i++)
        if (model->schema->tables[i].name && strcmp(model->schema->tables[i].name, name) == 0)
            return &model->schema->tables[i];
    return NULL;
}

const char *wlite_table_name(const wlite_table *table) {
    return table ? table->name : NULL;
}

size_t wlite_table_field_count(const wlite_table *table) {
    return table ? table->column_count : 0;
}

const wlite_field *wlite_table_field_at(const wlite_table *table, size_t index) {
    if (!table || index >= table->column_count) return NULL;
    return (const wlite_field *)&table->columns[index];
}

const wlite_field *wlite_table_field(const wlite_table *table, const char *name) {
    if (!table || !name) return NULL;
    for (size_t i = 0; i < table->column_count; i++)
        if (table->columns[i].name && strcmp(table->columns[i].name, name) == 0)
            return (const wlite_field *)&table->columns[i];
    return NULL;
}

const char *wlite_table_sql_name(const wlite_table *table) {
    return table ? table->name : NULL;
}

const char *wlite_field_name(const wlite_field *field) {
    return field ? ((const WlColumn *)field)->name : NULL;
}

wlite_col_type wlite_field_type(const wlite_field *field) {
    return field ? ((const WlColumn *)field)->affinity : WL_COL_NONE;
}

unsigned wlite_field_flags(const wlite_field *field) {
    if (!field) return 0;
    const WlColumn *c = (const WlColumn *)field;
    unsigned f = 0;
    if (c->not_null) f |= 1;
    if (c->primary_key) f |= 2;
    if (c->is_unique) f |= 4;
    if (c->autoincrement) f |= 8;
    if (c->is_generated) f |= 16;
    return f;
}

int wlite_field_is_nullable(const wlite_field *field) {
    return field ? !((const WlColumn *)field)->not_null : 0;
}

int wlite_field_is_primary_key(const wlite_field *field) {
    return field ? ((const WlColumn *)field)->primary_key : 0;
}

int wlite_field_is_unique(const wlite_field *field) {
    return field ? ((const WlColumn *)field)->is_unique : 0;
}

int wlite_field_is_autoincrement(const wlite_field *field) {
    return field ? ((const WlColumn *)field)->autoincrement : 0;
}

/* ── Migration (thin wrapper) ────────────────────────────────────────── */

wlite_result wlite_migrate(wlite_db *db, const wlite_model *model) {
    if (!db || !model || !model->schema) return WLITE_INVALID_ARGUMENT;
    /* For now: introspect current, diff against model, apply plan */
    WlSchema *current = wl_schema_introspect(db->sqlite, NULL);
    if (!current) current = calloc(1, sizeof(WlSchema)); /* empty DB */
    WlPlan *plan = wl_plan_migration(current, model->schema, NULL);
    wl_schema_free(current);
    if (!plan) return WLITE_ERROR;
    wlite_result rc = wl_apply_plan(db, plan, NULL);
    wl_plan_free(plan);
    return rc;
}
