---
title: C API Reference
description: Complete C API reference for libwlite.
---

# libwlite C API Reference

## Database

```c
wlite_result wlite_open(const char *path, wlite_db **out);
wlite_result wlite_open_ex(const char *path, const wlite_open_options *opts, wlite_db **out);
void wlite_close(wlite_db *db);
```

Open or close a SQLite database. `wlite_open_ex` accepts options for journal mode, foreign keys, and busy timeout.

## SQL Execution

```c
wlite_result wlite_execute(wlite_db *db, const char *sql, int64_t *rows_affected);
```

Execute a SQL statement that returns no result set. Returns the number of rows affected.

## Prepared Statements

```c
wlite_result wlite_prepare(wlite_db *db, const char *sql, wlite_stmt **out);
wlite_result wlite_bind_int64(wlite_stmt *stmt, int index, int64_t value);
wlite_result wlite_bind_double(wlite_stmt *stmt, int index, double value);
wlite_result wlite_bind_text(wlite_stmt *stmt, int index, const char *value);
wlite_result wlite_bind_null(wlite_stmt *stmt, int index);
wlite_result wlite_step(wlite_stmt *stmt);
void wlite_stmt_finalize(wlite_stmt *stmt);
```

Prepare, bind parameters, step through results, and finalize statements. Parameters are 1-indexed.

## Column Access

```c
int wlite_column_count(wlite_stmt *stmt);
const char *wlite_column_name(wlite_stmt *stmt, int column);
wlite_value_type wlite_column_type(wlite_stmt *stmt, int column);
int64_t wlite_column_int64(wlite_stmt *stmt, int column);
double wlite_column_double(wlite_stmt *stmt, int column);
const char *wlite_column_text(wlite_stmt *stmt, int column);
```

Access column metadata and values after a successful `wlite_step`. Columns are 0-indexed.

## Model

```c
wlite_result wlite_model_load_file(const char *path, wlite_model **out);
wlite_result wlite_model_load_memory(const void *data, size_t size, wlite_model **out);
wlite_result wlite_model_load_compiled(const void *data, size_t size, wlite_model **out);
wlite_result wlite_model_validate(const wlite_model *model);
void wlite_model_free(wlite_model *model);
```

Load a `.wlite` model from file, memory, or a compiled `.wlitem` binary. Validate the model structure. Models are immutable after loading.

## Model Introspection

```c
size_t wlite_model_table_count(const wlite_model *model);
const wlite_table *wlite_model_table(const wlite_model *model, const char *name);
const wlite_table *wlite_model_table_at(const wlite_model *model, size_t index);
const char *wlite_table_name(const wlite_table *table);
size_t wlite_table_field_count(const wlite_table *table);
const wlite_field *wlite_table_field(const wlite_table *table, const char *name);
const wlite_field *wlite_table_field_at(const wlite_table *table, size_t index);
const char *wlite_field_name(const wlite_field *field);
wlite_col_type wlite_field_type(const wlite_field *field);
int wlite_field_is_nullable(const wlite_field *field);
int wlite_field_is_primary_key(const wlite_field *field);
int wlite_field_is_unique(const wlite_field *field);
int wlite_field_is_autoincrement(const wlite_field *field);
```

Navigate the loaded model: tables, fields, and their properties. Tables and fields are borrowed from the model (do not free separately).

## Transactions

```c
wlite_result wlite_begin(wlite_db *db, wlite_tx **out);
wlite_result wlite_commit(wlite_tx *tx);
wlite_result wlite_rollback(wlite_tx *tx);
void wlite_tx_free(wlite_tx *tx);
```

Begin, commit, or rollback a transaction. Always free the transaction handle.

## Savepoints

```c
wlite_result wlite_savepoint(wlite_tx *tx, const char *name);
wlite_result wlite_release(wlite_tx *tx, const char *name);
wlite_result wlite_rollback_to(wlite_tx *tx, const char *name);
```

Nested transaction control via savepoints.

## Migration

```c
wlite_result wlite_diff(wlite_db *db, const wlite_model *model, WlPlan **out_plan);
wlite_result wlite_migrate(wlite_db *db, const wlite_model *model);
size_t wlite_plan_count(const WlPlan *plan);
```

Compare a live database against a model and generate a migration plan. `wlite_migrate` executes the plan. `wlite_diff` returns the plan without applying it.

## Error Handling

```c
const char *wlite_strerror(wlite_result result);
void wlite_error_free(wlite_error *err);
```

Convert error codes to human-readable strings. Free error objects when done.

## Version

```c
#define WLITE_ABI_VERSION 1
int wlite_abi_version(void);
const char *wlite_version(void);
```

Query the ABI version and library version string.

## Memory

```c
void wlite_free(void *ptr);
char *wlite_strdup(const char *s);
```

Utility functions for memory management. Use `wlite_free` to release memory allocated by libwlite.
