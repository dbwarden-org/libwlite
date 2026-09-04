---
title: C API Reference
description: Complete C API reference for libwlite, with examples.
---

# libwlite C API Reference

## Database

```c
wlite_result wlite_open(const char *path, wlite_db **out);
wlite_result wlite_open_ex(const char *path, const wlite_open_options *opts, wlite_db **out);
void wlite_close(wlite_db *db);
```

Open or close a SQLite database.

```c
wlite_db *db = NULL;
wlite_result r = wlite_open("app.db", &db);
if (r != WLITE_OK) {
    fprintf(stderr, "Failed to open database: %s\n", wlite_strerror(r));
    return 1;
}

// Use the database...

wlite_close(db);
```

`wlite_open_ex` accepts options for journal mode, foreign keys, and busy timeout:

```c
wlite_open_options opts = {
    .journal_mode = "WAL",
    .foreign_keys = 1,
    .busy_timeout = 5000,
};
wlite_open_ex("app.db", &opts, &db);
```

## SQL Execution

```c
wlite_result wlite_execute(wlite_db *db, const char *sql, int64_t *rows_affected);
```

Execute a SQL statement that returns no result set (DDL, DML):

```c
int64_t affected = 0;
wlite_execute(db, "CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT NOT NULL)", &affected);
wlite_execute(db, "INSERT INTO users (name) VALUES ('Alice')", &affected);
printf("%lld rows affected\n", affected);
```

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

```c
wlite_stmt *stmt = NULL;
wlite_prepare(db, "SELECT * FROM users WHERE id = ? AND name = ?", &stmt);

wlite_bind_int64(stmt, 1, 42);
wlite_bind_text(stmt, 2, "Alice");

while (wlite_step(stmt) == WLITE_OK) {
    // Process row
    const char *name = wlite_column_text(stmt, 1);
    printf("User: %s\n", name);
}

wlite_stmt_finalize(stmt);
```

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

```c
wlite_stmt *stmt = NULL;
wlite_prepare(db, "SELECT id, name, email FROM users", &stmt);

int cols = wlite_column_count(stmt);
printf("Query returns %d columns\n", cols);

while (wlite_step(stmt) == WLITE_OK) {
    int64_t id = wlite_column_int64(stmt, 0);
    const char *name = wlite_column_text(stmt, 1);
    const char *email = wlite_column_text(stmt, 2);

    wlite_value_type type = wlite_column_type(stmt, 2);
    if (type == WLITE_NULL) {
        printf("%lld: %s (no email)\n", id, name);
    } else {
        printf("%lld: %s <%s>\n", id, name, email);
    }
}

wlite_stmt_finalize(stmt);
```

## Model

```c
wlite_result wlite_model_load_file(const char *path, wlite_model **out);
wlite_result wlite_model_load_memory(const void *data, size_t size, wlite_model **out);
wlite_result wlite_model_load_compiled(const void *data, size_t size, wlite_model **out);
wlite_result wlite_model_validate(const wlite_model *model);
void wlite_model_free(wlite_model *model);
```

Load a `.wlite` model from file, memory, or a compiled `.wlitem` binary.

```c
wlite_model *model = NULL;
wlite_result r = wlite_model_load_file("schema.wlite", &model);
if (r != WLITE_OK) {
    fprintf(stderr, "Failed to load model: %s\n", wlite_strerror(r));
    return 1;
}

// Validate the model structure
r = wlite_model_validate(model);
if (r != WLITE_OK) {
    fprintf(stderr, "Invalid model: %s\n", wlite_strerror(r));
}

// Use the model for migration or introspection...

wlite_model_free(model);
```

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

Navigate the loaded model. Tables and fields are borrowed (do not free separately).

```c
wlite_model *model = NULL;
wlite_model_load_file("schema.wlite", &model);

size_t table_count = wlite_model_table_count(model);
printf("Model has %zu tables\n", table_count);

for (size_t i = 0; i < table_count; i++) {
    const wlite_table *table = wlite_model_table_at(model, i);
    printf("Table: %s\n", wlite_table_name(table));

    size_t field_count = wlite_table_field_count(table);
    for (size_t j = 0; j < field_count; j++) {
        const wlite_field *field = wlite_table_field_at(table, j);
        printf("  %s (%d)", wlite_field_name(field), wlite_field_type(field));
        if (wlite_field_is_primary_key(field)) printf(" PK");
        if (wlite_field_is_unique(field)) printf(" UNIQUE");
        if (!wlite_field_is_nullable(field)) printf(" NOT NULL");
        printf("\n");
    }
}

wlite_model_free(model);
```

## Transactions

```c
wlite_result wlite_begin(wlite_db *db, wlite_tx **out);
wlite_result wlite_commit(wlite_tx *tx);
wlite_result wlite_rollback(wlite_tx *tx);
void wlite_tx_free(wlite_tx *tx);
```

Begin, commit, or rollback a transaction. Always free the transaction handle.

```c
wlite_tx *tx = NULL;
wlite_begin(db, &tx);

wlite_execute(db, "INSERT INTO users (name) VALUES ('Alice')", NULL);
wlite_execute(db, "INSERT INTO users (name) VALUES ('Bob')", NULL);

// Commit or rollback
if (some_error) {
    wlite_rollback(tx);
} else {
    wlite_commit(tx);
}

wlite_tx_free(tx);
```

## Savepoints

```c
wlite_result wlite_savepoint(wlite_tx *tx, const char *name);
wlite_result wlite_release(wlite_tx *tx, const char *name);
wlite_result wlite_rollback_to(wlite_tx *tx, const char *name);
```

Nested transaction control via savepoints:

```c
wlite_tx *tx = NULL;
wlite_begin(db, &tx);

wlite_execute(db, "INSERT INTO users (name) VALUES ('Alice')", NULL);

wlite_savepoint(tx, "sp1");
wlite_execute(db, "INSERT INTO users (name) VALUES ('Bob')", NULL);

// Undo just the second insert
wlite_rollback_to(tx, "sp1");
wlite_release(tx, "sp1");

// Alice is still inserted, Bob is not
wlite_commit(tx);
wlite_tx_free(tx);
```

## Migration

```c
wlite_result wlite_diff(wlite_db *db, const wlite_model *model, WlPlan **out_plan);
wlite_result wlite_migrate(wlite_db *db, const wlite_model *model);
size_t wlite_plan_count(const WlPlan *plan);
```

Compare a live database against a model and generate a migration plan. `wlite_migrate` executes the plan. `wlite_diff` returns the plan without applying it.

```c
// Preview changes
wlite_plan *plan = NULL;
wlite_diff(db, model, &plan);
size_t steps = wlite_plan_count(plan);
printf("Migration has %zu steps\n", steps);

// Apply
wlite_migrate(db, model);
```

## Error Handling

```c
const char *wlite_strerror(wlite_result result);
void wlite_error_free(wlite_error *err);
```

Convert error codes to human-readable strings:

```c
wlite_result r = wlite_open("missing.db", &db);
if (r != WLITE_OK) {
    fprintf(stderr, "Error %d: %s\n", r, wlite_strerror(r));
}
```

## Version

```c
#define WLITE_ABI_VERSION 1
int wlite_abi_version(void);
const char *wlite_version(void);
```

Query the ABI version and library version string:

```c
printf("wlite %s (ABI v%d)\n", wlite_version(), wlite_abi_version());
```

## Memory

```c
void wlite_free(void *ptr);
char *wlite_strdup(const char *s);
```

Utility functions for memory management. Use `wlite_free` to release memory allocated by libwlite.

```c
char *copy = wlite_strdup("hello");
// Use the copy...
wlite_free(copy);
```
