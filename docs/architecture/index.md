---
title: Architecture Overview
description: High-level overview of libwlite's architecture and design.
---

# Architecture Overview

libwlite is the C library at the heart of dbwarden's schema management. It reads `.wlite` model files, compares them against live SQLite databases, and produces or executes the SQL needed to bring the database into sync. Every binding (Rust, Python, Go, C#, Zig, Ruby) talks to the same C ABI.

## Design principles

libwlite follows three design principles inherited from dbwarden:

1. **Single source of truth.** The `.wlite` model file is the only thing you edit. Everything else (SQL, diffs, snapshots) is derived from it.

2. **Plain SQL output.** No runtime, no generated Python, no hidden ORM behavior. The output is SQL you can read, review, and execute anywhere.

3. **Small and composable.** libwlite is a single C library with no dependencies beyond SQLite3. Bindings are thin wrappers. Complexity lives in the algorithm, not the framework.

## System layout

```
                *.wlite
                   |
                   v
              +---------+
              | libwlite|
              |    C    |
              +----+----+
                   |
                 SQLite3
```

libwlite reads `.wlite` files, performs schema comparison, and returns SQL or executes migrations directly. There is no CLI wrapper in the library layer. All entry points are C functions exposed through the public header.

## Language bindings

```
libwlite C ABI
      |
      +-- C/C++ (direct / header-only wrapper)
      +-- Rust  (FFI via cc crate)
      +-- Python (ctypes)
      +-- Go (cgo)
      +-- C# (P/Invoke)
      +-- Zig (@cImport)
      +-- Ruby (FFI)
```

All bindings go through the C ABI. No binding reimplements libwlite semantics. Each binding wraps the same set of C functions: model load, database open, migrate, query, transactions.

## Module map

libwlite is organized into focused modules, each handling one stage of the pipeline:

| Module | Stage | Purpose |
|--------|-------|---------|
| `parser.c` | Input | Parse `.wlite` DSL into `WlSchema` |
| `schema.c` | Core | Schema lifecycle, database API, model API |
| `introspect.c` | Input | Read live SQLite database into `WlSchema` |
| `diff.c` | Compare | Compare two `WlSchema` instances |
| `planner.c` | Plan | Convert diff into ordered SQL statements |
| `migrate.c` | Execute | Run the plan, record checksums, verify |
| `query.c` | Query | Prepared statements, parameter binding |
| `record.c` | Query | Generic row access |
| `tx.c` | Query | Transactions and savepoints |
| `compile.c` | Format | `.wlitem` compiled binary model format |
| `serialize.c` | Format | JSON and DSL serialization |
| `schema_inspect.c` | Bridge | Live DB to `WlSchema` conversion |

## Data flow summary

1. **Parse**: `parser.c` reads a `.wlite` file into a `WlSchema`.
2. **Introspect**: `introspect.c` reads the live database into a second `WlSchema`.
3. **Diff**: `diff.c` compares the two schemas and produces a `WlDiff`.
4. **Plan**: `planner.c` converts the diff into a `WlPlan` of SQL statements.
5. **Migrate**: `migrate.c` executes the plan within a transaction.

Each stage is documented in detail in the following pages:

- [Data Flow](data-flow.md): The complete pipeline from model to SQL
- [Migration Internals](migration-internals.md): How diffs become SQL, step by step
- [SQLite Patterns](sqlite-patterns.md): Table rebuilds, type normalization, collapse logic
- [Memory and Errors](memory-and-errors.md): Ownership rules, error handling, thread safety

## Where complexity lives

Most SQLite schema tools are simple: they diff two schemas and produce ALTER TABLE statements. The complexity in libwlite (and dbwarden) comes from the cases where SQLite's ALTER TABLE is not enough:

- Changing a column type requires rebuilding the entire table
- Adding a NOT NULL column without a DEFAULT requires rebuilding
- Changing a constraint requires rebuilding
- Multiple rebuilds on the same table should be collapsed into one

These cases are rare in small projects but common in production schemas. libwlite handles them automatically, the same way dbwarden does.

## Size and performance

libwlite is designed to be small:

- Single C library, approximately 12 source files
- Compiles in under a second on modern hardware
- The static library is under 200KB

Migration speed is limited by SQLite I/O, not by libwlite's diffing or planning. For typical schemas (under 100 tables), the planning phase takes microseconds.

## API surface

The public API is split into several categories. Each category maps to a module and a specific responsibility.

### Model functions

Model functions load and query `.wlite` files. A model is the parsed representation of your schema definition.

```c
wlite_result wlite_model_load_file(const char *path, wlite_model **out);
wlite_result wlite_model_load_memory(const void *data, size_t size, wlite_model **out);
wlite_result wlite_model_load_compiled(const void *data, size_t size, wlite_model **out);
void wlite_model_free(wlite_model *model);
```

`wlite_model_load_file` reads from a file path. `wlite_model_load_memory` accepts raw DSL text in a buffer. `wlite_model_load_compiled` loads a pre-compiled binary model. All three produce an owned `wlite_model` that the caller must free with `wlite_model_free`.

### Database functions

Database functions open and close SQLite connections.

```c
wlite_result wlite_open(const char *path, wlite_db **out);
wlite_result wlite_open_ex(const char *path, const wlite_open_options *options, wlite_db **out);
void wlite_close(wlite_db *db);
```

`wlite_open` creates a new `wlite_db` that wraps a SQLite connection. `wlite_open_ex` accepts additional options (read-only, create, foreign keys, busy timeout). `wlite_close` closes the connection and frees the wrapper.

### Migration functions

Migration functions compare a model against a database and produce or execute SQL.

```c
wlite_result wlite_diff(wlite_db *db, const wlite_model *model, WlPlan **out_plan);
wlite_result wlite_migrate(wlite_db *db, const wlite_model *model);
wlite_result wl_apply_plan(wlite_db *db, const WlPlan *plan, wlite_error **error);
wlite_result wl_rollback_last(wlite_db *db, wlite_error **error);
wlite_result wl_schema_verify(wlite_db *db, const WlSchema *expected, WlDiff **difference, wlite_error **error);
```

`wlite_diff` produces a plan without executing it. `wlite_migrate` executes the plan and records checksums. `wl_apply_plan` executes a previously computed plan with detailed error output. `wl_rollback_last` rolls back the most recent migration. `wl_schema_verify` compares the live database against an expected schema.

### Query functions

Query functions execute SQL and return results. They follow a prepare-step-finalize pattern similar to SQLite's own API.

```c
wlite_result wlite_prepare(wlite_db *db, const char *sql, wlite_stmt **out);
wlite_result wlite_step(wlite_stmt *stmt);
void wlite_stmt_reset(wlite_stmt *stmt);
void wlite_stmt_finalize(wlite_stmt *stmt);
```

Parameter binding uses type-specific functions:

```c
wlite_result wlite_bind_null(wlite_stmt *stmt, int index);
wlite_result wlite_bind_int64(wlite_stmt *stmt, int index, int64_t value);
wlite_result wlite_bind_double(wlite_stmt *stmt, int index, double value);
wlite_result wlite_bind_text(wlite_stmt *stmt, int index, const char *value);
wlite_result wlite_bind_text_n(wlite_stmt *stmt, int index, const char *value, size_t length);
wlite_result wlite_bind_blob(wlite_stmt *stmt, int index, const void *data, size_t size);
```

Column access reads values from a stepped statement:

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

### Record functions

Record functions provide generic row access from a stepped statement.

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

### Transaction functions

Transaction functions provide transaction and savepoint management.

```c
wlite_result wlite_begin(wlite_db *db, wlite_tx **out);
wlite_result wlite_commit(wlite_tx *tx);
wlite_result wlite_rollback(wlite_tx *tx);
void wlite_tx_free(wlite_tx *tx);
```

Savepoints nest within a transaction:

```c
wlite_result wlite_savepoint(wlite_tx *tx, const char *name);
wlite_result wlite_release(wlite_tx *tx, const char *name);
wlite_result wlite_rollback_to(wlite_tx *tx, const char *name);
```

### Plan functions

Plan functions inspect and free migration plans.

```c
size_t wlite_plan_count(const WlPlan *plan);
void wl_plan_free(WlPlan *plan);
```

### Schema inspection functions

Schema functions parse, inspect, and introspect schemas.

```c
WlSchema *wl_schema_parse(const char *source, size_t length, wlite_error **error);
WlSchema *wl_schema_load(const char *path, wlite_error **error);
WlSchema *wl_schema_introspect(struct sqlite3 *db, wlite_error **error);
WlSchema *wl_schema_inspect(wlite_db *db, wlite_error **error);
void wl_schema_free(WlSchema *schema);
const char *wl_schema_model_name(const WlSchema *schema);
int wl_schema_model_version(const WlSchema *schema);
```

Schema hashing and serialization:

```c
char *wl_schema_hash(const WlSchema *schema);
int wl_schema_write_json(const WlSchema *schema, wlite_writer *w, wlite_error **error);
int wl_schema_write_dsl(const WlSchema *schema, wlite_writer *w, wlite_error **error);
```

### Model introspection functions

These functions query the structure of a loaded model.

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

Field (column) introspection:

```c
const char *wlite_field_name(const wlite_field *field);
wlite_col_type wlite_field_type(const wlite_field *field);
unsigned wlite_field_flags(const wlite_field *field);
int wlite_field_is_nullable(const wlite_field *field);
int wlite_field_is_primary_key(const wlite_field *field);
int wlite_field_is_unique(const wlite_field *field);
int wlite_field_is_autoincrement(const wlite_field *field);
```

### Model validation

```c
wlite_result wlite_model_validate(const wlite_model *model);
```

### Model compilation

```c
int wl_model_compile(const WlSchema *schema, const char *path);
WlSchema *wl_model_load_compiled_raw(const void *data, size_t size);
```

### Error handling

```c
const char *wlite_strerror(wlite_result result);
void wlite_error_free(wlite_error *err);
```

### Utility functions

```c
int wlite_abi_version(void);
const char *wlite_version(void);
wlite_result wlite_execute(wlite_db *db, const char *sql, int64_t *rows_affected);
char *wlite_strdup(const char *s);
void wlite_free(void *p);
void wl_sqlite_capabilities(struct sqlite3 *db, wlite_sqlite_caps *caps);
```

## Module details

Each module in the library has a single responsibility. This section provides a brief description of each.

### parser.c

The parser reads `.wlite` DSL text and produces a `WlSchema`. It is a hand-written recursive descent parser with no external dependencies. It handles tokenization, comment stripping, and error reporting with line and column numbers. The parser is the entry point for all model operations.

### schema.c

Schema.c manages the lifecycle of `WlSchema` and its child objects (tables, fields, indexes, views, triggers). It provides accessor functions for querying the schema structure. All schema objects are allocated as a single contiguous block for cache efficiency.

### introspect.c

The introspector reads a live SQLite database and produces a `WlSchema` in the same format as the parser output. It uses SQLite PRAGMAs to discover columns, constraints, indexes, and foreign keys. Type normalization happens here.

### diff.c

The diff engine compares two `WlSchema` instances and produces a `WlDiff`. Each difference is classified as additive, subtractive, alterative, or rebuild. The diff is the input to the planner.

### planner.c

The planner converts a `WlDiff` into a `WlPlan` of ordered SQL statements. It handles dependency ordering, rebuild expansion, and collapse logic. The plan is the output that the migration engine executes.

### migrate.c

The migration engine executes a `WlPlan` within a transaction. It records checksums after successful migration and provides rollback on failure. This is the module that actually modifies the database.

### query.c

Query.c provides prepared statement management. It wraps SQLite's prepare-step-finalize pattern with type-safe parameter binding. It also handles error propagation from SQLite.

### record.c

Record.c provides generic row access. It reads column values from a step result by index or name, with type conversion. It is the bridge between SQLite results and application code.

### tx.c

Transaction.c provides transaction and savepoint management. It wraps begin-commit-rollback with automatic cleanup on error.

### compile.c

Compile.c handles the `.wlitem` binary model format. This is an optional compiled form of the model that skips parsing on subsequent loads. It is useful for large schemas where parsing time matters.

### serialize.c

Serialization.c provides JSON and DSL output for schemas. It can convert a `WlSchema` back to `.wlite` DSL text or to JSON for inspection.

### schema_inspect.c

Schema inspect.c bridges the gap between a raw SQLite connection and a `WlSchema`. It coordinates the introspection process and handles edge cases like virtual tables and contentless tables.

## Compilation

libwlite is compiled as a static library. Link against it with `-lwlite` and `-lsqlite3`.

```
gcc -o myapp myapp.c -lwlite -lsqlite3
```

Or include it directly in your build system. The library has no configuration options. All features are always available.

### Build from source

To build libwlite from source:

```
git clone https://github.com/dbwarden-org/libwlite.git
cd libwlite
make
```

This produces `libwlite.a` in the build directory. Include the header from `include/wlite/wlite.h`.

### CMake integration

libwlite provides a `CMakeLists.txt` for projects that use CMake:

```cmake
add_subdirectory(libwlite)
target_link_libraries(myapp wlite)
```

### pkg-config

libwlite installs a `.pc` file for pkg-config:

```
pkg-config --cflags --libs wlite
```

## Thread safety

libwlite objects follow specific thread safety rules. Models are immutable after loading and can be shared across threads. Database connections, statements, and transactions are not thread-safe and must not be shared. See [Memory and Errors](memory-and-errors.md) for details.

## Error model

Every function returns a `wlite_result` code. Zero means success. Non-zero means error. The `wlite_strerror` function converts codes to human-readable messages. For detailed error information (the failing SQL, the SQLite error message), use functions that accept a `wlite_error**` out-parameter. See [Memory and Errors](memory-and-errors.md) for the full error code table.

## Naming conventions

libwlite follows consistent naming conventions:

- Public types: `wlite_` prefix with snake_case (e.g., `wlite_model`, `wlite_db`)
- Public functions: `wlite_` prefix with snake_case (e.g., `wlite_open`, `wlite_migrate`)
- Constants: `WLITE_` prefix with SCREAMING_SNAKE_CASE (e.g., `WLITE_OK`, `WLITE_ERROR`)
- Internal types: `Wl` prefix with PascalCase (e.g., `WlSchema`, `WlDiff`)
- Internal functions: `wl_` prefix with snake_case (e.g., `wl_plan_migration`, `wl_schema_hash`)

## Compatibility

libwlite is compatible with SQLite 3.25 and later. It uses `ALTER TABLE ... RENAME COLUMN` when available (3.25+) and falls back to a rebuild for older versions. It uses `ALTER TABLE ... DROP COLUMN` when available (3.35+) and falls back to a rebuild for older versions.

The library is tested on Linux, macOS, and Windows. It uses no platform-specific APIs. The only system dependency is SQLite3. The C standard library functions used are limited to what is available in C99 and POSIX.

## Summary

libwlite is a focused C library that does one thing: bring a SQLite database into sync with a `.wlite` model file. It handles the hard cases (table rebuilds, constraint changes, type normalization) automatically. It exposes a clean C API that bindings can wrap in any language. It depends only on SQLite3 and the C standard library.

The architecture is designed for simplicity. Each module has a single responsibility. The data flow is linear and deterministic. There is no hidden state, no configuration files, and no side channels. The result is a library that is easy to understand, easy to integrate, and easy to trust.

For detailed information about each stage of the pipeline, see the linked documentation pages. For the full API reference, see the header file `include/wlite/wlite.h`.
