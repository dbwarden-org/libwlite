/*
 * wlite.h — Public API for libwlite
 *
 * A tiny SQLite schema and migration engine for applications and embedded systems.
 * Copyright (c) 2026 dbwarden contributors — MIT License
 */

#ifndef WLITE_H
#define WLITE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── ABI Version ─────────────────────────────────────────────────────── */

#define WLITE_ABI_VERSION 1

int wlite_abi_version(void);

/* ── Version ─────────────────────────────────────────────────────────── */

#define WLITE_VERSION_MAJOR 0
#define WLITE_VERSION_MINOR 2
#define WLITE_VERSION_PATCH 0

const char *wlite_version(void);

/* ── Error codes ─────────────────────────────────────────────────────── */

typedef enum {
    WLITE_OK = 0,
    WLITE_ERROR,
    WLITE_INVALID_ARGUMENT,
    WLITE_OUT_OF_MEMORY,
    WLITE_IO_ERROR,
    WLITE_PARSE_ERROR,
    WLITE_MODEL_ERROR,
    WLITE_SQLITE_ERROR,
    WLITE_CONSTRAINT_ERROR,
    WLITE_NOT_FOUND,
    WLITE_BUSY,
    WLITE_TRANSACTION_ERROR,
    WLITE_ERR_SYNTAX = WLITE_PARSE_ERROR,
    WLITE_ERR_NULL_PTR = WLITE_INVALID_ARGUMENT,
    WLITE_ERR_IO = WLITE_IO_ERROR,
} wlite_result;

typedef struct wlite_error {
    wlite_result code;
    char *message;
    char *subsystem;
    char *object;
    int sqlite_code;
    int line;
} wlite_error;

void wlite_error_free(wlite_error *err);

/* ── Error string lookup ─────────────────────────────────────────────── */

const char *wlite_strerror(wlite_result result);

/* ── Opaque types ────────────────────────────────────────────────────── */

typedef struct wlite_db wlite_db;
typedef struct wlite_model wlite_model;
typedef struct wlite_stmt wlite_stmt;
typedef struct wlite_record wlite_record;
typedef struct wlite_tx wlite_tx;

/* Model types are concrete (needed for introspection API) */
typedef struct WlTable wlite_table;
typedef struct WlColumn wlite_field;

/* ── Schema (internal, used by introspection) ────────────────────────── */

typedef enum {
    WL_COL_NONE = 0, WL_COL_INTEGER, WL_COL_REAL, WL_COL_TEXT, WL_COL_BLOB, WL_COL_ANY,
} wlite_col_type;

typedef enum {
    WL_FK_NO_ACTION = 0, WL_FK_RESTRICT, WL_FK_SET_NULL, WL_FK_SET_DEFAULT, WL_FK_CASCADE,
} wlite_fk_action;

typedef struct WlColumn {
    char *name;
    char *type_name;
    wlite_col_type affinity;
    int not_null;
    int primary_key;
    int autoincrement;
    int is_unique;
    int is_generated;
    int is_stored;
    char *default_expr;
    char *collate;
    char *generated_expr;
    char *fk_table;
    char *fk_column;
    char *fk_on_delete;
    char *fk_on_update;
} WlColumn;

typedef struct {
    char **columns;
    size_t column_count;
    char *ref_table;
    char **ref_columns;
    size_t ref_column_count;
    wlite_fk_action on_delete;
    wlite_fk_action on_update;
} WlForeignKey;

typedef struct { char *name; char *expression; } WlCheck;
typedef struct { char *name; char **columns; size_t column_count; } WlUnique;
typedef struct { char **columns; size_t column_count; } WlPrimaryKey;

typedef struct {
    char *name;
    char *table;
    char **columns;
    size_t column_count;
    char *expression;
    char *where_clause;
    int unique;
} WlIndex;

typedef struct { char *name; char *sql; } WlView;
typedef struct { char *name; char *table; char *sql; } WlTrigger;

typedef struct WlTable {
    char *name;
    WlColumn *columns;
    size_t column_count;
    WlPrimaryKey primary_key;
    WlForeignKey *foreign_keys;
    size_t foreign_key_count;
    WlCheck *checks;
    size_t check_count;
    WlUnique *uniques;
    size_t unique_count;
    int strict;
    int without_rowid;
    char *comment;
} WlTable;

typedef struct {
    uint32_t version;
    WlTable *tables;
    size_t table_count;
    WlIndex *indexes;
    size_t index_count;
    WlView *views;
    size_t view_count;
    WlTrigger *triggers;
    size_t trigger_count;
    char *model_name;
    int model_version;
} WlSchema;

void wl_schema_free(WlSchema *schema);

/* ── Model identity ──────────────────────────────────────────────────── */

const char *wl_schema_model_name(const WlSchema *schema);
int wl_schema_model_version(const WlSchema *schema);

/* ── Schema parsing ──────────────────────────────────────────────────── */

WlSchema *wl_schema_parse(const char *source, size_t length, wlite_error **error);
WlSchema *wl_schema_load(const char *path, wlite_error **error);

/* ── SQLite introspection ────────────────────────────────────────────── */

struct sqlite3;
WlSchema *wl_schema_introspect(struct sqlite3 *db, wlite_error **error);

/* ── Diff ────────────────────────────────────────────────────────────── */

typedef enum {
    WL_DIFF_ADD_TABLE, WL_DIFF_DROP_TABLE, WL_DIFF_RENAME_TABLE,
    WL_DIFF_ADD_COLUMN, WL_DIFF_DROP_COLUMN, WL_DIFF_RENAME_COLUMN, WL_DIFF_ALTER_COLUMN,
    WL_DIFF_ADD_INDEX, WL_DIFF_DROP_INDEX, WL_DIFF_ALTER_INDEX,
    WL_DIFF_ADD_CHECK, WL_DIFF_DROP_CHECK, WL_DIFF_ADD_UNIQUE, WL_DIFF_DROP_UNIQUE,
    WL_DIFF_ADD_FKEY, WL_DIFF_DROP_FKEY,
    WL_DIFF_ALTER_TABLE_OPTIONS, WL_DIFF_ALTER_VIEW, WL_DIFF_ALTER_TRIGGER,
    WL_DIFF_REBUILD_TABLE,
} WlDiffOp;

typedef enum {
    WL_SAFETY_SAFE = 0, WL_SAFETY_REQUIRES_REBUILD, WL_SAFETY_DESTRUCTIVE,
    WL_SAFETY_CONDITIONAL, WL_SAFETY_IRREVERSIBLE,
} WlSafety;

typedef struct {
    WlDiffOp op;
    WlSafety safety;
    char *table;
    char *object;
    char *detail;
} WlDiffEntry;

typedef struct {
    WlDiffEntry *entries;
    size_t entry_count;
} WlDiff;

WlDiff *wl_schema_diff(const WlSchema *current, const WlSchema *desired, wlite_error **error);
void wl_diff_free(WlDiff *diff);

/* ── Migration plan ──────────────────────────────────────────────────── */

typedef enum {
    WL_PLAN_CREATE_TABLE, WL_PLAN_DROP_TABLE, WL_PLAN_RENAME_TABLE,
    WL_PLAN_ADD_COLUMN, WL_PLAN_DROP_COLUMN, WL_PLAN_RENAME_COLUMN, WL_PLAN_ALTER_COLUMN,
    WL_PLAN_REBUILD_TABLE, WL_PLAN_CREATE_INDEX, WL_PLAN_DROP_INDEX,
    WL_PLAN_ADD_CHECK, WL_PLAN_DROP_CHECK, WL_PLAN_ADD_UNIQUE, WL_PLAN_DROP_UNIQUE,
    WL_PLAN_ADD_FKEY, WL_PLAN_DROP_FKEY, WL_PLAN_CUSTOM_SQL,
} WlPlanOp;

typedef struct {
    WlPlanOp op;
    WlSafety safety;
    char *sql;
    char *rollback_sql;
    char *table;
    char *detail;
    int is_non_atomic;
} WlPlanStep;

typedef struct {
    WlPlanStep *steps;
    size_t step_count;
    char *schema_hash_before;
    char *schema_hash_after;
} WlPlan;

WlPlan *wl_plan_migration(const WlSchema *current, const WlSchema *desired, wlite_error **error);
void wl_plan_free(WlPlan *plan);

/* ── Single-call diff (db + model → plan) ────────────────────────────── */

wlite_result wlite_diff(wlite_db *db, const wlite_model *model, WlPlan **out_plan);

/* ── Plan inspection ─────────────────────────────────────────────────── */

size_t wlite_plan_count(const WlPlan *plan);

/* ── Database API ────────────────────────────────────────────────────── */

typedef struct { int readonly; int create; int foreign_keys; int busy_timeout_ms; } wlite_open_options;

wlite_result wlite_open(const char *path, wlite_db **out);
wlite_result wlite_open_ex(const char *path, const wlite_open_options *options, wlite_db **out);
void wlite_close(wlite_db *db);

/* ── SQL execution (convenience) ─────────────────────────────────────── */

wlite_result wlite_execute(wlite_db *db, const char *sql, int64_t *rows_affected);

/* ── Model API ───────────────────────────────────────────────────────── */

wlite_result wlite_model_load_file(const char *path, wlite_model **out);
wlite_result wlite_model_load_memory(const void *data, size_t size, wlite_model **out);
wlite_result wlite_model_load_compiled(const void *data, size_t size, wlite_model **out);
void wlite_model_free(wlite_model *model);

/* ── Model compilation ───────────────────────────────────────────────── */

int wl_model_compile(const WlSchema *schema, const char *path);
WlSchema *wl_model_load_compiled_raw(const void *data, size_t size);

/* ── Model validation ────────────────────────────────────────────────── */

wlite_result wlite_model_validate(const wlite_model *model);

/* ── Model introspection ─────────────────────────────────────────────── */

size_t wlite_model_table_count(const wlite_model *model);
const wlite_table *wlite_model_table_at(const wlite_model *model, size_t index);
const wlite_table *wlite_model_table(const wlite_model *model, const char *name);
const char *wlite_table_name(const wlite_table *table);
size_t wlite_table_field_count(const wlite_table *table);
const wlite_field *wlite_table_field_at(const wlite_table *table, size_t index);
const wlite_field *wlite_table_field(const wlite_table *table, const char *name);
const char *wlite_table_sql_name(const wlite_table *table);

/* field is WlColumn* alias */
const char *wlite_field_name(const wlite_field *field);
wlite_col_type wlite_field_type(const wlite_field *field);
unsigned wlite_field_flags(const wlite_field *field);
int wlite_field_is_nullable(const wlite_field *field);
int wlite_field_is_primary_key(const wlite_field *field);
int wlite_field_is_unique(const wlite_field *field);
int wlite_field_is_autoincrement(const wlite_field *field);

/* ── Migration ───────────────────────────────────────────────────────── */

wlite_result wlite_migrate(wlite_db *db, const wlite_model *model);
wlite_result wl_apply_plan(wlite_db *db, const WlPlan *plan, wlite_error **error);
wlite_result wl_rollback_last(wlite_db *db, wlite_error **error);
wlite_result wl_schema_verify(wlite_db *db, const WlSchema *expected, WlDiff **difference, wlite_error **error);

/* ── Query API ───────────────────────────────────────────────────────── */

wlite_result wlite_prepare(wlite_db *db, const char *sql, wlite_stmt **out);
wlite_result wlite_bind_null(wlite_stmt *stmt, int index);
wlite_result wlite_bind_int64(wlite_stmt *stmt, int index, int64_t value);
wlite_result wlite_bind_double(wlite_stmt *stmt, int index, double value);
wlite_result wlite_bind_text(wlite_stmt *stmt, int index, const char *value);
wlite_result wlite_bind_text_n(wlite_stmt *stmt, int index, const char *value, size_t length);
wlite_result wlite_bind_blob(wlite_stmt *stmt, int index, const void *data, size_t size);
wlite_result wlite_step(wlite_stmt *stmt);
void wlite_stmt_reset(wlite_stmt *stmt);
void wlite_stmt_finalize(wlite_stmt *stmt);

/* ── Column access ───────────────────────────────────────────────────── */

typedef enum {
    WLITE_TYPE_NULL, WLITE_TYPE_INTEGER, WLITE_TYPE_REAL, WLITE_TYPE_TEXT, WLITE_TYPE_BLOB
} wlite_value_type;

int wlite_column_count(wlite_stmt *stmt);
const char *wlite_column_name(wlite_stmt *stmt, int column);
wlite_value_type wlite_column_type(wlite_stmt *stmt, int column);
int64_t wlite_column_int64(wlite_stmt *stmt, int column);
double wlite_column_double(wlite_stmt *stmt, int column);
const char *wlite_column_text(wlite_stmt *stmt, int column);
const void *wlite_column_blob(wlite_stmt *stmt, int column);
size_t wlite_column_bytes(wlite_stmt *stmt, int column);

/* ── Record API ──────────────────────────────────────────────────────── */

wlite_record *wlite_record_from_stmt(wlite_stmt *stmt);
void wlite_record_free(wlite_record *record);
int wlite_record_column_count(const wlite_record *record);
const char *wlite_record_column_name(const wlite_record *record, int index);
wlite_value_type wlite_record_column_type(const wlite_record *record, int index);
int wlite_record_find(const wlite_record *record, const char *name);
int64_t wlite_record_int64(const wlite_record *record, int index);
double wlite_record_double(const wlite_record *record, int index);
const char *wlite_record_text(const wlite_record *record, int index);
const void *wlite_record_blob(const wlite_record *record, int index);
size_t wlite_record_blob_bytes(const wlite_record *record, int index);

/* ── Transaction API ─────────────────────────────────────────────────── */

wlite_result wlite_begin(wlite_db *db, wlite_tx **out);
wlite_result wlite_commit(wlite_tx *tx);
wlite_result wlite_rollback(wlite_tx *tx);
void wlite_tx_free(wlite_tx *tx);

/* ── Savepoints ──────────────────────────────────────────────────────── */

wlite_result wlite_savepoint(wlite_tx *tx, const char *name);
wlite_result wlite_release(wlite_tx *tx, const char *name);
wlite_result wlite_rollback_to(wlite_tx *tx, const char *name);

/* ── Schema inspection ───────────────────────────────────────────────── */

WlSchema *wl_schema_inspect(wlite_db *db, wlite_error **error);

/* ── Schema hashing ──────────────────────────────────────────────────── */

char *wl_schema_hash(const WlSchema *schema);

/* ── Serialization ───────────────────────────────────────────────────── */

typedef struct wlite_writer {
    void *ctx;
    int (*write)(struct wlite_writer *w, const char *data, size_t len);
} wlite_writer;

int wl_schema_write_json(const WlSchema *schema, wlite_writer *w, wlite_error **error);
int wl_schema_write_dsl(const WlSchema *schema, wlite_writer *w, wlite_error **error);

/* ── SQLite capabilities ─────────────────────────────────────────────── */

typedef struct { int has_rename_column; int has_drop_column; int has_strict;
    int has_generated; int has_without_rowid; } wlite_sqlite_caps;

void wl_sqlite_capabilities(struct sqlite3 *db, wlite_sqlite_caps *caps);

/* ── Memory ──────────────────────────────────────────────────────────── */

char *wlite_strdup(const char *s);
void wlite_free(void *p);

#ifdef __cplusplus
}
#endif

#endif /* WLITE_H */
