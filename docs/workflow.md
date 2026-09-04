---
title: Migration Workflow
description: Complete migration workflow from C, including schema inspection, hashing, model compilation, and tracking.
---

# Migration Workflow

This page walks through the complete libwlite migration workflow from C. Every
step is shown with the corresponding C API calls, data structures, and patterns
for building migration tools.

## Overview

The standard libwlite workflow follows a predictable sequence:

```
load model -> open database -> diff -> migrate -> verify
```

Each step maps to one or more C API calls. The library handles parsing,
introspection, diffing, planning, and execution.

## Loading a model

A model is the parsed representation of a `.wlite` file. Load it from a file
path or from a buffer in memory.

### From file

```c
wlite_model *model = NULL;
wlite_result rc = wlite_model_load_file("schema.wlite", &model);
if (rc != WLITE_OK) {
    fprintf(stderr, "failed to load model: %d\n", rc);
    return EXIT_FAILURE;
}
```

### From memory

```c
const char *source = "model User { ... }";
wlite_model *model = NULL;
wlite_result rc = wlite_model_load(source, strlen(source), &model);
```

### From compiled .wlitem

```c
wlite_model *model = NULL;
wlite_result rc = wlite_model_load_compiled("schema.wlitem", &model);
```

The compiled form skips parsing entirely. See
[Model Compilation](#model-compilation) for details.

## Opening a database

```c
wlite_db *db = NULL;
wlite_result rc = wlite_open("app.db", &db);
if (rc != WLITE_OK) {
    fprintf(stderr, "failed to open database: %d\n", rc);
    wlite_model_free(model);
    return EXIT_FAILURE;
}
```

`wlite_open` creates a new database wrapper around a SQLite connection. If the
file does not exist, SQLite creates it. The wrapper handles foreign key
enforcement, busy timeout, and other pragmas automatically.

## Schema inspection

libwlite reads the live database schema using `PRAGMA table_info()` and
`PRAGMA index_list()`. This produces a `WlSchema` that represents the current
state.

### Programmatic inspection

```c
wlite_error *error = NULL;
WlSchema *live = wl_schema_introspect(db->sqlite, &error);
if (!live) {
    fprintf(stderr, "introspection failed: %s\n", error->message);
    wlite_error_free(error);
    wlite_close(db);
    wlite_model_free(model);
    return EXIT_FAILURE;
}
```

The introspected schema is a full `WlSchema` with tables, columns, indexes,
foreign keys, and constraints. It is the same data structure used for parsed
models.

### What is captured

For each table:

- Table name, strict mode, without rowid flag
- Column names, types, nullability, primary key status
- Column default values and collation sequences
- Foreign key definitions with on delete and on update actions
- CHECK constraints
- UNIQUE constraints
- Indexes (including partial and composite indexes)

The introspection is a point-in-time snapshot. Changes to the database after
introspection are not reflected.

## Schema diffing

Compare the live schema against the model to find differences:

```c
WlDiff *diff = wl_schema_diff(live, model->schema, &error);
```

### Inspecting the diff

```c
if (diff->entry_count == 0) {
    printf("Schema is up to date\n");
} else {
    printf("Found %zu differences\n", diff->entry_count);
    for (size_t i = 0; i < diff->entry_count; i++) {
        WlDiffEntry *entry = &diff->entries[i];
        printf("  %s: %s.%s\n", entry->action, entry->table, entry->column);
    }
}
```

### Diff entry structure

Each `WlDiffEntry` contains:

- `action`: What changed (add, drop, alter, rebuild)
- `table`: Table name
- `column`: Column name (NULL for table-level changes)
- `detail`: Description of the change

The diff is a lightweight structure. It does not own the schemas it references.

## Planning migrations

Convert the diff into an ordered sequence of SQL statements:

```c
WlPlan *plan = wl_plan_migration(live, model->schema, &error);
```

### Inspecting the plan

```c
size_t count = wlite_plan_count(plan);
printf("Migration has %zu steps\n", count);
```

### Applying the plan

```c
wlite_result rc = wl_apply_plan(db, plan, &error);
if (rc != WLITE_OK) {
    fprintf(stderr, "migration failed: %s\n", error->message);
    wlite_error_free(error);
}
```

The plan executes inside a transaction. If any step fails, the transaction
rolls back and the database is unchanged.

## Single-call migration

For the common case, libwlite provides a single function that handles
introspection, diffing, planning, and execution:

```c
wlite_result rc = wlite_migrate(db, model);
```

This is equivalent to:

```c
WlSchema *live = wl_schema_introspect(db->sqlite, NULL);
WlPlan *plan = wl_plan_migration(live, model->schema, NULL);
wl_apply_plan(db, plan, NULL);
```

But with proper error handling and resource cleanup.

## Schema verification

Verify that the database schema matches the model without modifying anything:

```c
WlDiff *difference = NULL;
wlite_result rc = wl_schema_verify(db, model->schema, &difference, &error);

if (rc == WLITE_OK) {
    printf("Schema matches\n");
} else if (rc == WLITE_NOT_FOUND) {
    printf("Schema drift detected (%zu differences)\n", difference->entry_count);
    wl_diff_free(difference);
}
```

`wl_schema_verify` is read-only. It does not modify the database. It returns
`WLITE_OK` if the schemas match, `WLITE_NOT_FOUND` if they differ.

This is the primary function for CI validation.

## Schema hashing

libwlite computes an FNV-1a hash of the schema. The hash is a 16-character
hexadecimal string that captures the complete schema structure.

```c
char *hash = wl_schema_hash(model->schema);
if (hash) {
    printf("Schema hash: %s\n", hash);
    free(hash);
}
```

Output:

```
Schema hash: a1b2c3d4e5f6a7b8
```

### What the hash covers

The FNV-1a hash is computed over:

- Schema version and table count
- For each table: name, column count, strict mode, without rowid flag
- For each column: name, type, not_null, primary_key, unique, autoincrement,
  default expression, collation, foreign key table and column
- Index count, names, and table references
- View count and trigger count

### What the hash does not cover

- Row data
- Table statistics
- Triggers and views (beyond their count)

### The FNV-1a algorithm

libwlite uses FNV-1a (Fowler-Noll-Vo), not SHA-256. FNV-1a is a non-cryptographic hash function with excellent distribution and minimal computational overhead.

The implementation:

```c
static uint64_t fnv1a(const char *data, size_t len) {
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < len; i++) {
        h ^= (uint8_t)data[i];
        h *= 1099511628211ULL;
    }
    return h;
}
```

The initial value is the FNV offset basis. Each byte is XORed into the hash,
then multiplied by the FNV prime. This produces a 64-bit value that is
formatted as 16 hexadecimal characters.

FNV-1a is chosen over SHA-256 because:

- It is faster (no cryptographic overhead)
- It has excellent avalanche properties (small input changes produce large hash
  changes)
- It does not require a cryptographic library
- Schema fingerprinting does not need collision resistance

## The _wlite_migrations tracking table

When you run `wlite_migrate`, libwlite creates a tracking table in the
database:

```sql
CREATE TABLE IF NOT EXISTS _wlite_migrations (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    checksum TEXT NOT NULL,
    applied_at INTEGER NOT NULL
);
```

### Columns

| Column | Type | Description |
|--------|------|-------------|
| `id` | INTEGER | Auto-incrementing migration identifier |
| `name` | TEXT | Name of the migration (typically the model name) |
| `checksum` | TEXT | FNV-1a hash of the migration SQL |
| `applied_at` | INTEGER | Unix timestamp when the migration was applied |

### How it works

1. Before applying a migration, libwlite checks if a row with the same checksum
   already exists in `_wlite_migrations`.
2. If the checksum exists, the migration is skipped (idempotent).
3. If the checksum does not exist, the migration SQL runs inside a transaction.
4. After successful execution, a new row is inserted with the checksum and
   timestamp.
5. If the migration fails, the transaction rolls back and no row is inserted.

### Querying migration history

You can query the table directly:

```c
wlite_stmt *stmt;
wlite_prepare(db,
    "SELECT id, name, checksum, datetime(applied_at, 'unixepoch') "
    "FROM _wlite_migrations ORDER BY id",
    &stmt);

while (wlite_step(stmt) == WLITE_ROW) {
    printf("Migration %d: %s (applied %s)\n",
        wlite_column_int(stmt, 0),
        wlite_column_text(stmt, 1),
        wlite_column_text(stmt, 3));
}

wlite_stmt_finalize(stmt);
```

### Rolling back

```c
wlite_rollback_last(db, &error);
```

This removes the most recent migration record from `_wlite_migrations`. It does
not reverse the SQL changes. It only marks the migration as not applied so that
the next `wlite_migrate` will re-apply it.

## Model compilation

libwlite can compile a parsed `.wlite` model into a binary `.wlitem` format.
This compiled form skips parsing on subsequent loads.

### Compiling

```c
wlite_result rc = wl_model_compile(model->schema, "schema.wlitem");
if (rc != WLITE_OK) {
    fprintf(stderr, "compilation failed: %d\n", rc);
}
```

### Loading compiled models

```c
wlite_model *compiled = NULL;
wlite_result rc = wlite_model_load_compiled("schema.wlitem", &compiled);
```

### When to compile

Compile your model when:

- You have a large schema (hundreds of tables) and parsing time matters
- You want to distribute a pre-compiled binary model with your application
- You are building a tool that loads the same model repeatedly

For small to medium schemas, parsing a `.wlite` file takes microseconds. The
compiled form is an optimization, not a requirement.

### The .wlitem binary format

The compiled format uses a compact binary layout:

```
Header:
  magic:          4 bytes ("WLIT" = 0x54494C57)
  version:        4 bytes (uint32, currently 1)
  model_name:     length-prefixed string
  model_version:  4 bytes (int32)
  table_count:    4 bytes (uint32)

Per table:
  name:           length-prefixed string
  flags:          1 byte (bit 0 = strict, bit 1 = without_rowid)
  comment:        length-prefixed string
  column_count:   4 bytes (uint32)
  columns:
    name:              length-prefixed string
    type_name:         length-prefixed string
    flags:             1 byte (bit 0 = not_null, bit 1 = primary_key,
                               bit 2 = unique, bit 3 = autoincrement,
                               bit 4 = is_generated, bit 5 = is_stored)
    default_expr:      length-prefixed string
    collate:           length-prefixed string
    generated_expr:    length-prefixed string
    fk_table:          length-prefixed string
    fk_column:         length-prefixed string
  pk_count:       4 bytes (uint32)
  pk_columns:     pk_count length-prefixed strings
  fk_count:       4 bytes (uint32)
  foreign_keys:
    column_count:    4 bytes (uint32)
    columns:         column_count length-prefixed strings
    ref_table:       length-prefixed string
    ref_column_count: 4 bytes (uint32)
    ref_columns:     ref_column_count length-prefixed strings
    on_delete:       1 byte (enum value)
    on_update:       1 byte (enum value)
  check_count:    4 bytes (uint32)
  checks:
    name:            length-prefixed string
    expression:      length-prefixed string
  unique_count:   4 bytes (uint32)
  uniques:
    name:            length-prefixed string
    column_count:    4 bytes (uint32)
    columns:         column_count length-prefixed strings
```

A length-prefixed string is a 4-byte uint32 length followed by that many bytes
of UTF-8 data. An empty or NULL string has length 0 and no data bytes.

## Complete workflow example

Here is a complete C program that loads a model, opens a database, migrates,
and verifies:

```c
#include <stdio.h>
#include <stdlib.h>
#include <wlite/wlite.h>

int main(void) {
    wlite_model *model = NULL;
    wlite_db    *db    = NULL;
    wlite_error *error = NULL;
    wlite_result rc;

    /* Load the model */
    rc = wlite_model_load_file("schema.wlite", &model);
    if (rc != WLITE_OK) {
        fprintf(stderr, "failed to load model: %d\n", rc);
        return EXIT_FAILURE;
    }

    /* Open the database */
    rc = wlite_open("app.db", &db);
    if (rc != WLITE_OK) {
        fprintf(stderr, "failed to open database: %d\n", rc);
        wlite_model_free(model);
        return EXIT_FAILURE;
    }

    /* Migrate */
    rc = wlite_migrate(db, model);
    if (rc != WLITE_OK) {
        fprintf(stderr, "migration failed: %s\n", error->message);
        wlite_error_free(error);
        wlite_close(db);
        wlite_model_free(model);
        return EXIT_FAILURE;
    }

    printf("Migration applied successfully\n");

    /* Verify */
    WlDiff *diff = NULL;
    rc = wl_schema_verify(db, model->schema, &diff, NULL);
    if (rc == WLITE_OK) {
        printf("Schema matches model\n");
    } else {
        printf("Schema drift detected (%zu differences)\n", diff->entry_count);
        wl_diff_free(diff);
    }

    /* Hash */
    char *hash = wl_schema_hash(model->schema);
    if (hash) {
        printf("Schema hash: %s\n", hash);
        free(hash);
    }

    /* Compile to .wlitem */
    rc = wl_model_compile(model->schema, "schema.wlitem");
    if (rc == WLITE_OK) {
        printf("Model compiled to schema.wlitem\n");
    }

    /* Cleanup */
    wlite_close(db);
    wlite_model_free(model);

    return EXIT_SUCCESS;
}
```

## Diffing without applying

If you want to see the diff without applying it:

```c
WlSchema *live = wl_schema_introspect(db->sqlite, NULL);
WlDiff *diff = wl_schema_diff(live, model->schema, NULL);

if (diff->entry_count == 0) {
    printf("Schema is up to date\n");
} else {
    printf("Migration needed:\n");
    for (size_t i = 0; i < diff->entry_count; i++) {
        printf("  %s\n", diff->entries[i].detail);
    }
}

wl_diff_free(diff);
wl_schema_free(live);
```

## Generating SQL without executing

To generate the migration SQL without executing it:

```c
WlSchema *live = wl_schema_introspect(db->sqlite, NULL);
WlPlan *plan = wl_plan_migration(live, model->schema, NULL);

if (plan) {
    for (size_t i = 0; i < plan->step_count; i++) {
        if (plan->steps[i].sql) {
            printf("%s\n", plan->steps[i].sql);
        }
    }
}

wl_plan_free(plan);
wl_schema_free(live);
```

## Resource cleanup order

Resources must be freed in the correct order to avoid use-after-free bugs:

1. Free diffs and plans before schemas
2. Close the database before freeing the model
3. Free the model last

```c
wl_diff_free(diff);       /* if applicable */
wl_plan_free(plan);       /* if applicable */
wlite_close(db);          /* close database first */
wlite_model_free(model);  /* then free model */
```

The reason is that the model may hold references to database-specific metadata.
Closing the database invalidates those references.

## Thread safety

libwlite is not thread-safe at the database level. Each thread should open its
own database connection. Model loading and schema hashing are safe to call from
multiple threads concurrently, as they operate on immutable data.

For multi-threaded applications:

```c
/* Each thread gets its own connection */
#pragma omp parallel
{
    wlite_db *thread_db = NULL;
    wlite_open("app.db", &thread_db);

    /* Use thread_db for queries */

    wlite_close(thread_db);
}
```

## Error handling patterns

### Simple error check

```c
wlite_result rc = wlite_migrate(db, model);
if (rc != WLITE_OK) {
    fprintf(stderr, "migration failed: %d\n", rc);
    return EXIT_FAILURE;
}
```

### Detailed error info

```c
wlite_error *error = NULL;
wlite_result rc = wlite_migrate(db, model, &error);
if (rc != WLITE_OK) {
    fprintf(stderr, "Error %d: %s\n", error->code, error->message);
    if (error->sqlite_code) {
        fprintf(stderr, "SQLite code: %d\n", error->sqlite_code);
    }
    wlite_error_free(error);
}
```

### Cleanup on error

Always clean up in error paths. Use a goto-based cleanup pattern:

```c
wlite_result rc;
wlite_model *model = NULL;
wlite_db *db = NULL;

rc = wlite_model_load_file("schema.wlite", &model);
if (rc != WLITE_OK) goto error;

rc = wlite_open("app.db", &db);
if (rc != WLITE_OK) goto error;

rc = wlite_migrate(db, model);
if (rc != WLITE_OK) goto error;

printf("Success\n");

cleanup:
    if (db) wlite_close(db);
    if (model) wlite_model_free(model);
    return rc == WLITE_OK ? EXIT_SUCCESS : EXIT_FAILURE;

error:
    fprintf(stderr, "Failed with code %d\n", rc);
    goto cleanup;
```

## Built-in SQL operations

libwlite provides convenience functions for common SQL operations:

### Execute statements

```c
wlite_exec(db, "PRAGMA journal_mode=WAL");
wlite_exec(db, "INSERT INTO users (name) VALUES ('alice')");
```

### Prepared statements with parameters

```c
wlite_stmt *stmt;
wlite_prepare(db, "SELECT * FROM users WHERE id = ?", &stmt);
wlite_bind_int(stmt, 1, 42);

while (wlite_step(stmt) == WLITE_ROW) {
    printf("%s\n", wlite_column_text(stmt, 0));
}

wlite_stmt_finalize(stmt);
```

### Named parameters

```c
wlite_prepare(db, "SELECT * FROM users WHERE name = :name", &stmt);
wlite_bind_named(stmt, ":name", "alice");
```

### Transactions

```c
wlite_exec(db, "BEGIN TRANSACTION");
wlite_exec(db, "INSERT INTO users (name) VALUES ('alice')");
wlite_exec(db, "INSERT INTO users (name) VALUES ('bob')");
wlite_exec(db, "COMMIT");
```

### Savepoints

```c
wlite_exec(db, "SAVEPOINT sp1");
wlite_exec(db, "INSERT INTO users (name) VALUES ('charlie')");
wlite_exec(db, "ROLLBACK TO sp1");  /* undo just charlie */
```

## Next steps

- Browse the [C API Reference](c-api.md) for the complete function list
- Read the [.wlite Grammar](grammar.md) for model file syntax
- Understand [Migration Internals](architecture/migration-internals.md) for
  how diffs become SQL
- Learn about [Memory and Errors](architecture/memory-and-errors.md) for
  correct resource management
