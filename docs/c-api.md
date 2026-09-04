---
title: C API Reference
description: Complete C API reference for libwlite, a tiny SQLite schema and migration engine.
---

# libwlite C API Reference

libwlite is a tiny SQLite schema and migration engine for applications and embedded systems. This document covers every public function declared in `wlite/wlite.h`.

## Table of Contents

- [Version and ABI](#version-and-abi)
- [Error Handling](#error-handling)
- [Database API](#database-api)
- [SQL Execution](#sql-execution)
- [Prepared Statements](#prepared-statements)
- [Column Access](#column-access)
- [Record API](#record-api)
- [Blob Support](#blob-support)
- [Transactions](#transactions)
- [Savepoints](#savepoints)
- [Model API](#model-api)
- [Model Introspection](#model-introspection)
- [Model Compilation](#model-compilation)
- [Schema Parsing and Inspection](#schema-parsing-and-inspection)
- [Schema Hashing](#schema-hashing)
- [Schema Serialization](#schema-serialization)
- [Schema Diffing](#schema-diffing)
- [Migration Plans](#migration-plans)
- [Migration Execution](#migration-execution)
- [Advanced Migration](#advanced-migration)
- [SQLite Capabilities](#sqlite-capabilities)
- [Memory Management](#memory-management)

## Version and ABI

```c
#define WLITE_ABI_VERSION 1
#define WLITE_VERSION_MAJOR 0
#define WLITE_VERSION_MINOR 2
#define WLITE_VERSION_PATCH 0

int wlite_abi_version(void);
const char *wlite_version(void);
```

Query the library version at compile time or runtime. The `WLITE_*` macros are available at compile time. The functions return the same values at runtime.

```c
printf("wlite %s (ABI v%d)\n", wlite_version(), wlite_abi_version());
```

## Error Handling

### Error Codes

```c
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
```

### Error Struct

```c
typedef struct wlite_error {
    wlite_result code;
    char *message;
    char *subsystem;
    char *object;
    int sqlite_code;
    int line;
} wlite_error;

void wlite_error_free(wlite_error *err);
const char *wlite_strerror(wlite_result result);
```

The `wlite_error` struct provides detailed error information when a function fails. Pass a pointer to a `wlite_error *` to receive error details. Always free the error with `wlite_error_free` when done.

`wlite_strerror` converts an error code to a human-readable string without allocating memory.

```c
wlite_error *err = NULL;
WlSchema *s = wl_schema_parse(NULL, 0, &err);
if (!s) {
    fprintf(stderr, "Error %d in %s: %s\n",
            err->code, err->subsystem, err->message);
    wlite_error_free(err);
}

const char *msg = wlite_strerror(WLITE_NOT_FOUND);
printf("Not found: %s\n", msg);
```

## Database API

### Opaque Types

```c
typedef struct wlite_db wlite_db;
```

`wlite_db` is an opaque handle to an open SQLite database connection.

### Open Options

```c
typedef struct {
    int readonly;
    int create;
    int foreign_keys;
    int busy_timeout_ms;
} wlite_open_options;
```

Field descriptions:

- `readonly` : open the database in read-only mode.
- `create` : create the database file if it does not exist.
- `foreign_keys` : enable `PRAGMA foreign_keys` after opening.
- `busy_timeout_ms` : set `PRAGMA busy_timeout` in milliseconds.

### Functions

```c
wlite_result wlite_open(const char *path, wlite_db **out);
wlite_result wlite_open_ex(const char *path, const wlite_open_options *options, wlite_db **out);
void wlite_close(wlite_db *db);
```

`wlite_open` opens a database with default options. Pass `":memory:"` for an in-memory database.

`wlite_open_ex` opens a database with explicit options.

`wlite_close` closes the database and releases all associated resources.

```c
wlite_db *db = NULL;
wlite_result r = wlite_open(":memory:", &db);
if (r != WLITE_OK) {
    fprintf(stderr, "Failed to open database: %s\n", wlite_strerror(r));
    return 1;
}

// Use the database...

wlite_close(db);
```

Using `wlite_open_ex` with options:

```c
wlite_open_options opts = {
    .readonly = 0,
    .create = 1,
    .foreign_keys = 1,
    .busy_timeout_ms = 5000,
};
wlite_db *db = NULL;
wlite_result r = wlite_open_ex("app.db", &opts, &db);
if (r != WLITE_OK) {
    fprintf(stderr, "Open failed: %s\n", wlite_strerror(r));
    return 1;
}

// Use the database...

wlite_close(db);
```

## SQL Execution

```c
wlite_result wlite_execute(wlite_db *db, const char *sql, int64_t *rows_affected);
```

Execute one or more SQL statements that do not return a result set. This is used for DDL statements like `CREATE TABLE` and DML statements like `INSERT`, `UPDATE`, and `DELETE`. Pass `NULL` for `rows_affected` if you do not need the count.

```c
int64_t affected = 0;

wlite_execute(db, "CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT NOT NULL)", &affected);
wlite_execute(db, "INSERT INTO users (name) VALUES ('Alice')", &affected);
wlite_execute(db, "INSERT INTO users (name) VALUES ('Bob')", &affected);

printf("%lld rows affected\n", (long long)affected);
```

Multiple statements in a single call:

```c
wlite_execute(db,
    "CREATE TABLE IF NOT EXISTS logs (id INTEGER PRIMARY KEY, msg TEXT);"
    "INSERT INTO logs (msg) VALUES ('started');",
    NULL);
```

## Prepared Statements

### Opaque Types

```c
typedef struct wlite_stmt wlite_stmt;
```

### Prepare, Bind, Step, Finalize

```c
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
```

`wlite_prepare` compiles a SQL string into a prepared statement. Parameters use `?` placeholders and are 1-indexed.

`wlite_step` advances to the next row. Returns `WLITE_OK` if a row is available, or `WLITE_NOT_FOUND` when there are no more rows.

`wlite_stmt_reset` resets the statement so it can be re-executed with new bindings.

`wlite_stmt_finalize` destroys the statement and releases its memory. Always finalize when done.

```c
wlite_stmt *stmt = NULL;
wlite_prepare(db, "SELECT * FROM users WHERE id = ? AND name = ?", &stmt);

wlite_bind_int64(stmt, 1, 42);
wlite_bind_text(stmt, 2, "Alice");

while (wlite_step(stmt) == WLITE_OK) {
    const char *name = wlite_column_text(stmt, 1);
    printf("User: %s\n", name);
}

wlite_stmt_finalize(stmt);
```

Batch insert example:

```c
wlite_stmt *ins = NULL;
wlite_prepare(db, "INSERT INTO users (name) VALUES (?)", &ins);

const char *names[] = { "Alice", "Bob", "Carol" };
for (int i = 0; i < 3; i++) {
    wlite_bind_text(ins, 1, names[i]);
    wlite_step(ins);
}

wlite_stmt_finalize(ins);
```

## Column Access

### Value Types

```c
typedef enum {
    WLITE_TYPE_NULL,
    WLITE_TYPE_INTEGER,
    WLITE_TYPE_REAL,
    WLITE_TYPE_TEXT,
    WLITE_TYPE_BLOB
} wlite_value_type;
```

### Functions

```c
int wlite_column_count(wlite_stmt *stmt);
const char *wlite_column_name(wlite_stmt *stmt, int column);
wlite_value_type wlite_column_type(wlite_stmt *stmt, int column);
int64_t wlite_column_int64(wlite_stmt *stmt, int column);
double wlite_column_double(wlite_stmt *stmt, int column);
const char *wlite_column_text(wlite_stmt *stmt, int column);
const void *wlite_column_blob(wlite_stmt *stmt, int column);
size_t wlite_column_bytes(wlite_stmt *stmt, int column);
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
    if (type == WLITE_TYPE_NULL) {
        printf("%lld: %s (no email)\n", (long long)id, name);
    } else {
        printf("%lld: %s <%s>\n", (long long)id, name, email);
    }
}

wlite_stmt_finalize(stmt);
```

Inspecting column names:

```c
wlite_stmt *stmt = NULL;
wlite_prepare(db, "SELECT id AS user_id, name AS user_name FROM users", &stmt);

int cols = wlite_column_count(stmt);
for (int i = 0; i < cols; i++) {
    printf("Column %d: %s\n", i, wlite_column_name(stmt, i));
}

wlite_stmt_finalize(stmt);
```

## Record API

```c
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
```

The Record API provides a higher-level interface for reading query results. Call `wlite_record_from_stmt` after a successful `wlite_step` to snapshot the current row into a `wlite_record`. The record owns its data and can be used after the statement is reset or finalized.

`wlite_record_find` returns the column index for a given name, or -1 if not found.

```c
wlite_stmt *stmt = NULL;
wlite_prepare(db, "SELECT id, name, email FROM users WHERE id = 1", &stmt);

if (wlite_step(stmt) == WLITE_OK) {
    wlite_record *rec = wlite_record_from_stmt(stmt);

    int idx = wlite_record_find(rec, "name");
    if (idx >= 0) {
        printf("name = %s\n", wlite_record_text(rec, idx));
    }

    printf("id = %lld\n", (long long)wlite_record_int64(rec, 0));

    wlite_record_free(rec);
}

wlite_stmt_finalize(stmt);
```

## Blob Support

Blobs are read and written through the prepared statement API using `wlite_bind_blob` and `wlite_column_blob`.

```c
wlite_result wlite_bind_blob(wlite_stmt *stmt, int index, const void *data, size_t size);
const void *wlite_column_blob(wlite_stmt *stmt, int column);
size_t wlite_column_bytes(wlite_stmt *stmt, int column);
```

Writing a blob:

```c
const unsigned char data[] = { 0xDE, 0xAD, 0xBE, 0xEF };
wlite_stmt *stmt = NULL;
wlite_prepare(db, "INSERT INTO files (name, content) VALUES (?, ?)", &stmt);
wlite_bind_text(stmt, 1, "binary.dat");
wlite_bind_blob(stmt, 2, data, sizeof(data));
wlite_step(stmt);
wlite_stmt_finalize(stmt);
```

Reading a blob:

```c
wlite_stmt *stmt = NULL;
wlite_prepare(db, "SELECT content FROM files WHERE name = ?", &stmt);
wlite_bind_text(stmt, 1, "binary.dat");

if (wlite_step(stmt) == WLITE_OK) {
    const void *blob = wlite_column_blob(stmt, 0);
    size_t len = wlite_column_bytes(stmt, 0);
    printf("Blob has %zu bytes\n", len);

    const unsigned char *p = (const unsigned char *)blob;
    for (size_t i = 0; i < len; i++) {
        printf("%02X ", p[i]);
    }
    printf("\n");
}

wlite_stmt_finalize(stmt);
```

Using `wlite_record` for blob access:

```c
if (wlite_step(stmt) == WLITE_OK) {
    wlite_record *rec = wlite_record_from_stmt(stmt);
    int idx = wlite_record_find(rec, "content");
    const void *blob = wlite_record_blob(rec, idx);
    size_t len = wlite_record_blob_bytes(rec, idx);
    printf("Blob size: %zu\n", len);
    wlite_record_free(rec);
}
```

## Transactions

### Opaque Types

```c
typedef struct wlite_tx wlite_tx;
```

### Functions

```c
wlite_result wlite_begin(wlite_db *db, wlite_tx **out);
wlite_result wlite_commit(wlite_tx *tx);
wlite_result wlite_rollback(wlite_tx *tx);
void wlite_tx_free(wlite_tx *tx);
```

Begin, commit, or rollback a transaction. Always free the transaction handle with `wlite_tx_free`.

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

Nested transaction control via savepoints. Savepoints allow partial rollback within a transaction.

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

## Model API

### Opaque Types

```c
typedef struct wlite_model wlite_model;
```

### Loading Models

```c
wlite_result wlite_model_load_file(const char *path, wlite_model **out);
wlite_result wlite_model_load_memory(const void *data, size_t size, wlite_model **out);
wlite_result wlite_model_load_compiled(const void *data, size_t size, wlite_model **out);
void wlite_model_free(wlite_model *model);
```

Load a `.wlite` model definition from a file, from a buffer in memory, or from a compiled `.wlitem` binary.

```c
wlite_model *model = NULL;
wlite_result r = wlite_model_load_file("schema.wlite", &model);
if (r != WLITE_OK) {
    fprintf(stderr, "Failed to load model: %s\n", wlite_strerror(r));
    return 1;
}

// Use the model...

wlite_model_free(model);
```

Loading from memory:

```c
const char *dsl = "model User { table \"users\" field id integer { primary_key } }";
wlite_model *model = NULL;
wlite_model_load_memory(dsl, strlen(dsl), &model);
```

### Model Validation

```c
wlite_result wlite_model_validate(const wlite_model *model);
```

Validate the structure of a loaded model. Returns `WLITE_OK` if valid.

```c
wlite_result r = wlite_model_validate(model);
if (r != WLITE_OK) {
    fprintf(stderr, "Invalid model: %s\n", wlite_strerror(r));
}
```

## Model Introspection

```c
size_t wlite_model_table_count(const wlite_model *model);
const wlite_table *wlite_model_table_at(const wlite_model *model, size_t index);
const wlite_table *wlite_model_table(const wlite_model *model, const char *name);
const char *wlite_table_name(const wlite_table *table);
size_t wlite_table_field_count(const wlite_table *table);
const wlite_field *wlite_table_field_at(const wlite_table *table, size_t index);
const wlite_field *wlite_table_field(const wlite_table *table, const char *name);
const char *wlite_table_sql_name(const wlite_table *table);
```

Navigate the loaded model. Tables and fields are borrowed pointers; do not free them separately.

### Field Accessors

```c
const char *wlite_field_name(const wlite_field *field);
wlite_col_type wlite_field_type(const wlite_field *field);
unsigned wlite_field_flags(const wlite_field *field);
int wlite_field_is_nullable(const wlite_field *field);
int wlite_field_is_primary_key(const wlite_field *field);
int wlite_field_is_unique(const wlite_field *field);
int wlite_field_is_autoincrement(const wlite_field *field);
```

The `wlite_col_type` enum:

```c
typedef enum {
    WL_COL_NONE = 0,
    WL_COL_INTEGER,
    WL_COL_REAL,
    WL_COL_TEXT,
    WL_COL_BLOB,
    WL_COL_ANY,
} wlite_col_type;
```

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

Looking up a specific table and field:

```c
const wlite_table *users = wlite_model_table(model, "users");
if (users) {
    const wlite_field *id = wlite_table_field(users, "id");
    if (id) {
        printf("id is primary key: %d\n", wlite_field_is_primary_key(id));
    }
}
```

## Model Compilation

```c
int wl_model_compile(const WlSchema *schema, const char *path);
WlSchema *wl_model_load_compiled_raw(const void *data, size_t size);
```

`wl_model_compile` serializes a parsed `WlSchema` into a compiled `.wlitem` binary file at the given path. Returns 0 on success.

`wl_model_load_compiled_raw` loads a compiled `.wlitem` buffer back into a `WlSchema` without going through the DSL parser.

```c
// Parse and compile
const char *dsl = "model User { table \"users\" field id integer { primary_key } }";
WlSchema *schema = wl_schema_parse(dsl, strlen(dsl), NULL);
wl_model_compile(schema, "user_model.wlitem");
wl_schema_free(schema);

// Load the compiled binary
FILE *f = fopen("user_model.wlitem", "rb");
fseek(f, 0, SEEK_END);
long sz = ftell(f);
fseek(f, 0, SEEK_SET);
char *buf = malloc(sz);
fread(buf, 1, sz, f);
fclose(f);

WlSchema *loaded = wl_model_load_compiled_raw(buf, sz);
free(buf);
// Use loaded schema...
wl_schema_free(loaded);
```

## Schema Parsing and Inspection

### Schema Types

```c
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
```

### Functions

```c
WlSchema *wl_schema_parse(const char *source, size_t length, wlite_error **error);
WlSchema *wl_schema_load(const char *path, wlite_error **error);
WlSchema *wl_schema_introspect(struct sqlite3 *db, wlite_error **error);
WlSchema *wl_schema_inspect(wlite_db *db, wlite_error **error);
void wl_schema_free(WlSchema *schema);
```

`wl_schema_parse` parses a DSL string into a `WlSchema`.

`wl_schema_load` reads a `.wlite` file from disk and parses it.

`wl_schema_introspect` reads the schema from a raw `sqlite3 *` connection.

`wl_schema_inspect` reads the schema from a `wlite_db *` handle.

### Model Identity

```c
const char *wl_schema_model_name(const WlSchema *schema);
int wl_schema_model_version(const WlSchema *schema);
```

Retrieve the model name and version declared in the DSL with `model_config`.

```c
const char *dsl =
    "model_config { name \"myapp\" version 3 }"
    "model User { table \"users\" field id integer { primary_key } }";

WlSchema *schema = wl_schema_parse(dsl, strlen(dsl), NULL);
printf("Model: %s v%d\n", wl_schema_model_name(schema), wl_schema_model_version(schema));
wl_schema_free(schema);
```

Introspecting a live database:

```c
wlite_db *db = NULL;
wlite_open(":memory:", &db);
wlite_execute(db, "CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT)", NULL);

WlSchema *live = wl_schema_inspect(db, NULL);
printf("Live schema has %zu tables\n", live->table_count);

wl_schema_free(live);
wlite_close(db);
```

## Schema Hashing

```c
char *wl_schema_hash(const WlSchema *schema);
```

Compute a deterministic hash string for a schema. The returned string must be freed with `free()`. Identical schemas always produce the same hash; different schemas produce different hashes.

```c
WlSchema *s1 = wl_schema_parse(dsl, strlen(dsl), NULL);
WlSchema *s2 = wl_schema_parse(dsl, strlen(dsl), NULL);

char *h1 = wl_schema_hash(s1);
char *h2 = wl_schema_hash(s2);
printf("Same schema, same hash: %s\n", strcmp(h1, h2) == 0 ? "yes" : "no");

free(h1);
free(h2);
wl_schema_free(s1);
wl_schema_free(s2);
```

## Schema Serialization

### Writer Type

```c
typedef struct wlite_writer {
    void *ctx;
    int (*write)(struct wlite_writer *w, const char *data, size_t len);
} wlite_writer;
```

A `wlite_writer` is a simple output sink. Set `ctx` to your context (e.g., a `FILE *`) and implement the `write` callback.

### Functions

```c
int wl_schema_write_json(const WlSchema *schema, wlite_writer *w, wlite_error **error);
int wl_schema_write_dsl(const WlSchema *schema, wlite_writer *w, wlite_error **error);
```

Serialize a `WlSchema` to JSON or DSL format through a writer.

```c
// Writer that writes to a FILE
static int file_writer(wlite_writer *w, const char *data, size_t len) {
    FILE *f = (FILE *)w->ctx;
    return (fwrite(data, 1, len, f) == len) ? 0 : -1;
}

// Serialize to JSON
FILE *out = fopen("schema.json", "w");
wlite_writer w = { .ctx = out, .write = file_writer };
wl_schema_write_json(schema, &w, NULL);
fclose(out);

// Serialize to DSL
out = fopen("schema.wlite", "w");
w.ctx = out;
wl_schema_write_dsl(schema, &w, NULL);
fclose(out);
```

## Schema Diffing

### Diff Types

```c
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
    WL_SAFETY_SAFE = 0,
    WL_SAFETY_REQUIRES_REBUILD,
    WL_SAFETY_DESTRUCTIVE,
    WL_SAFETY_CONDITIONAL,
    WL_SAFETY_IRREVERSIBLE,
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
```

### Functions

```c
WlDiff *wl_schema_diff(const WlSchema *current, const WlSchema *desired, wlite_error **error);
void wl_diff_free(WlDiff *diff);
```

Compare two schemas and return a list of differences. Each entry describes a single change and its safety level.

```c
WlSchema *current = wl_schema_inspect(db, NULL);
WlSchema *desired = wl_schema_parse(dsl, strlen(dsl), NULL);

WlDiff *diff = wl_schema_diff(current, desired, NULL);
if (diff) {
    for (size_t i = 0; i < diff->entry_count; i++) {
        WlDiffEntry *e = &diff->entries[i];
        printf("Op: %d, Safety: %d, Table: %s, Detail: %s\n",
               e->op, e->safety,
               e->table ? e->table : "(none)",
               e->detail ? e->detail : "");
    }
    wl_diff_free(diff);
}

wl_schema_free(current);
wl_schema_free(desired);
```

## Migration Plans

### Plan Types

```c
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
```

### Functions

```c
WlPlan *wl_plan_migration(const WlSchema *current, const WlSchema *desired, wlite_error **error);
void wl_plan_free(WlPlan *plan);
size_t wlite_plan_count(const WlPlan *plan);
```

`wl_plan_migration` generates a migration plan from two schemas. Each step contains SQL to apply and optional rollback SQL.

`wlite_plan_count` returns the number of steps in the plan.

```c
WlSchema *current = wl_schema_inspect(db, NULL);
WlSchema *desired = wl_schema_parse(dsl, strlen(dsl), NULL);

WlPlan *plan = wl_plan_migration(current, desired, NULL);
if (plan) {
    printf("Migration has %zu steps\n", wlite_plan_count(plan));
    printf("Schema hash before: %s\n", plan->schema_hash_before);
    printf("Schema hash after:  %s\n", plan->schema_hash_after);

    for (size_t i = 0; i < wlite_plan_count(plan); i++) {
        WlPlanStep *step = &plan->steps[i];
        printf("Step %zu: %s\n", i, step->sql ? step->sql : "(no sql)");
        if (step->rollback_sql) {
            printf("  Rollback: %s\n", step->rollback_sql);
        }
    }

    wl_plan_free(plan);
}

wl_schema_free(current);
wl_schema_free(desired);
```

## Migration Execution

```c
wlite_result wlite_diff(wlite_db *db, const wlite_model *model, WlPlan **out_plan);
wlite_result wlite_migrate(wlite_db *db, const wlite_model *model);
```

`wlite_diff` compares a live database against a model and returns the migration plan without applying it.

`wlite_migrate` compares and applies the migration in one call.

```c
// Preview changes before applying
WlPlan *plan = NULL;
wlite_result r = wlite_diff(db, model, &plan);
if (r == WLITE_OK && plan) {
    printf("Migration has %zu steps\n", wlite_plan_count(plan));

    // Inspect steps...
    for (size_t i = 0; i < wlite_plan_count(plan); i++) {
        printf("  Step %zu: %s\n", i, plan->steps[i].sql ? plan->steps[i].sql : "");
    }

    wl_plan_free(plan);
}

// Or apply directly
r = wlite_migrate(db, model);
if (r != WLITE_OK) {
    fprintf(stderr, "Migration failed: %s\n", wlite_strerror(r));
}
```

## Advanced Migration

### Functions

```c
wlite_result wl_apply_plan(wlite_db *db, const WlPlan *plan, wlite_error **error);
wlite_result wl_rollback_last(wlite_db *db, wlite_error **error);
wlite_result wl_schema_verify(wlite_db *db, const WlSchema *expected, WlDiff **difference, wlite_error **error);
```

`wl_apply_plan` executes a pre-computed migration plan against a database. Use this when you want to inspect or modify the plan before applying it.

`wl_rollback_last` undoes the most recent migration by executing rollback SQL stored in the plan steps. Returns `WLITE_OK` on success.

`wl_schema_verify` compares a live database schema against an expected schema. Returns `WLITE_OK` if they match. If they differ, returns `WLITE_NOT_FOUND` and populates the `difference` output parameter with a `WlDiff`.

```c
// Apply a plan directly
WlSchema *current = wl_schema_inspect(db, NULL);
WlSchema *desired = wl_schema_parse(dsl, strlen(dsl), NULL);
WlPlan *plan = wl_plan_migration(current, desired, NULL);

wlite_error *err = NULL;
wlite_result r = wl_apply_plan(db, plan, &err);
if (r != WLITE_OK) {
    fprintf(stderr, "Plan apply failed: %s\n", err->message);
    wlite_error_free(err);
}

wl_plan_free(plan);
wl_schema_free(current);
wl_schema_free(desired);
```

Verifying schema after migration:

```c
wlite_model *model = NULL;
wlite_model_load_file("schema.wlite", &model);
wlite_migrate(db, model);

WlSchema *expected = wl_schema_parse(dsl, strlen(dsl), NULL);
WlDiff *diff = NULL;
wlite_result r = wl_schema_verify(db, expected, &diff, NULL);
if (r == WLITE_OK) {
    printf("Schema matches expected\n");
} else if (r == WLITE_NOT_FOUND && diff) {
    printf("Schema diverges with %zu differences\n", diff->entry_count);
    wl_diff_free(diff);
}

wl_schema_free(expected);
wlite_model_free(model);
```

Rolling back the last migration:

```c
wlite_error *err = NULL;
wlite_result r = wl_rollback_last(db, &err);
if (r != WLITE_OK) {
    fprintf(stderr, "Rollback failed: %s\n", err->message);
    wlite_error_free(err);
}
```

## SQLite Capabilities

```c
typedef struct {
    int has_rename_column;
    int has_drop_column;
    int has_strict;
    int has_generated;
    int has_without_rowid;
} wlite_sqlite_caps;

void wl_sqlite_capabilities(struct sqlite3 *db, wlite_sqlite_caps *caps);
```

Query the SQLite library for feature support. The `caps` struct is populated with boolean flags indicating which features the runtime SQLite version supports.

```c
wlite_sqlite_caps caps;
wl_sqlite_capabilities(sqlite3_db_handle(db->sqlite), &caps);

printf("SQLite capabilities:\n");
printf("  RENAME COLUMN: %s\n", caps.has_rename_column ? "yes" : "no");
printf("  DROP COLUMN:   %s\n", caps.has_drop_column ? "yes" : "no");
printf("  STRICT:        %s\n", caps.has_strict ? "yes" : "no");
printf("  GENERATED:     %s\n", caps.has_generated ? "yes" : "no");
printf("  WITHOUT ROWID:  %s\n", caps.has_without_rowid ? "yes" : "no");
```

## Memory Management

```c
char *wlite_strdup(const char *s);
void wlite_free(void *p);
```

`wlite_strdup` duplicates a string using memory allocated by libwlite.

`wlite_free` releases memory allocated by libwlite functions. Use this for strings returned by libwlite that are documented as needing to be freed.

```c
char *copy = wlite_strdup("hello");
printf("Copy: %s\n", copy);
wlite_free(copy);
```

## Complete Example

The following example demonstrates a full workflow: loading a model, opening a database, migrating, querying, and inspecting the schema.

```c
#include <stdio.h>
#include <string.h>
#include "wlite/wlite.h"

int main(void) {
    // Load the model
    wlite_model *model = NULL;
    wlite_result r = wlite_model_load_file("schema.wlite", &model);
    if (r != WLITE_OK) {
        fprintf(stderr, "Model load failed: %s\n", wlite_strerror(r));
        return 1;
    }

    // Validate the model
    r = wlite_model_validate(model);
    if (r != WLITE_OK) {
        fprintf(stderr, "Model invalid: %s\n", wlite_strerror(r));
        wlite_model_free(model);
        return 1;
    }

    // Open the database
    wlite_open_options opts = {
        .readonly = 0,
        .create = 1,
        .foreign_keys = 1,
        .busy_timeout_ms = 5000,
    };
    wlite_db *db = NULL;
    r = wlite_open_ex("app.db", &opts, &db);
    if (r != WLITE_OK) {
        fprintf(stderr, "Open failed: %s\n", wlite_strerror(r));
        wlite_model_free(model);
        return 1;
    }

    // Migrate
    r = wlite_migrate(db, model);
    if (r != WLITE_OK) {
        fprintf(stderr, "Migrate failed: %s\n", wlite_strerror(r));
        wlite_close(db);
        wlite_model_free(model);
        return 1;
    }

    // Insert data in a transaction
    wlite_tx *tx = NULL;
    wlite_begin(db, &tx);
    wlite_stmt *ins = NULL;
    wlite_prepare(db, "INSERT INTO users (name) VALUES (?)", &ins);
    wlite_bind_text(ins, 1, "Alice");
    wlite_step(ins);
    wlite_bind_text(ins, 1, "Bob");
    wlite_step(ins);
    wlite_stmt_finalize(ins);
    wlite_commit(tx);
    wlite_tx_free(tx);

    // Query
    wlite_stmt *stmt = NULL;
    wlite_prepare(db, "SELECT id, name FROM users", &stmt);
    while (wlite_step(stmt) == WLITE_OK) {
        int64_t id = wlite_column_int64(stmt, 0);
        const char *name = wlite_column_text(stmt, 1);
        printf("%lld: %s\n", (long long)id, name);
    }
    wlite_stmt_finalize(stmt);

    // Verify schema
    WlSchema *live = wl_schema_inspect(db, NULL);
    char *hash = wl_schema_hash(live);
    printf("Schema hash: %s\n", hash);
    free(hash);
    wl_schema_free(live);

    // Cleanup
    wlite_close(db);
    wlite_model_free(model);
    return 0;
}
```
