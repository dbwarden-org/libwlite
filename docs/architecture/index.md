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
- No dynamic memory allocation for schema operations (uses caller-provided buffers)
- Compiles in under a second on modern hardware
- The static library is under 200KB

Migration speed is limited by SQLite I/O, not by libwlite's diffing or planning. For typical schemas (under 100 tables), the planning phase takes microseconds.

## API surface

The public API is split into several categories. Each category maps to a module and a specific responsibility.

### Model functions

Model functions load and query `.wlite` files. A model is the parsed representation of your schema definition.

```c
wlite_result wlite_model_load(const char *source, size_t length, wlite_model **model);
wlite_result wlite_model_load_file(const char *path, wlite_model **model);
wlite_result wlite_model_free(wlite_model *model);
```

`wlite_model_load` accepts raw DSL text. `wlite_model_load_file` reads from a file path. Both produce an owned `wlite_model` that the caller must free with `wlite_model_free`.

### Database functions

Database functions open and close SQLite connections. libwlite does not create a database object; it wraps the existing `sqlite3*` handle.

```c
wlite_result wlite_open(const char *path, wlite_db **db);
wlite_result wlite_close(wlite_db *db);
```

`wlite_open` creates a new `wlite_db` that wraps a SQLite connection. `wlite_close` closes the connection and frees the wrapper.

### Migration functions

Migration functions compare a model against a database and produce or execute SQL.

```c
wlite_result wlite_diff(wlite_db *db, wlite_model *model, wlite_plan **plan);
wlite_result wlite_migrate(wlite_db *db, wlite_model *model);
wlite_result wlite_migrate_with_error(wlite_db *db, wlite_model *model, wlite_error **error);
```

`wlite_diff` produces a plan without executing it. `wlite_migrate` executes the plan and records checksums. `wlite_migrate_with_error` does the same but returns detailed error information on failure.

### Query functions

Query functions execute SQL and return results. They follow a prepare-step-finalize pattern similar to SQLite's own API.

```c
wlite_result wlite_prepare(wlite_db *db, const char *sql, wlite_stmt **stmt);
wlite_result wlite_step(wlite_stmt *stmt);
wlite_result wlite_finalize(wlite_stmt *stmt);
wlite_result wlite_bind(wlite_stmt *stmt, int index, int type, const void *value);
```

### Transaction functions

Transaction functions provide transaction and savepoint management.

```c
wlite_result wlite_tx_begin(wlite_db *db, wlite_tx **tx);
wlite_result wlite_tx_commit(wlite_tx *tx);
wlite_result wlite_tx_rollback(wlite_tx *tx);
wlite_result wlite_tx_free(wlite_tx *tx);
```

### Plan functions

Plan functions inspect and free migration plans.

```c
size_t wlite_plan_count(const wlite_plan *plan);
const wlite_plan_step *wlite_plan_step_at(const wlite_plan *plan, size_t index);
wlite_result wlite_plan_free(wlite_plan *plan);
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

Every function returns a `wlite_result` code. Zero means success. Non-zero means error. The `wlite_strerror` function converts codes to human-readable messages. For detailed error information (the failing SQL, the SQLite error message), use the `_with_error` variants. See [Memory and Errors](memory-and-errors.md) for the full error code table.

## Naming conventions

libwlite follows consistent naming conventions:

- Public types: `wlite_` prefix with snake_case (e.g., `wlite_model`, `wlite_plan`)
- Public functions: `wlite_` prefix with snake_case (e.g., `wlite_open`, `wlite_migrate`)
- Constants: `WLITE_` prefix with SCREAMING_SNAKE_CASE (e.g., `WLITE_OK`, `WLITE_ERROR`)
- Internal types: `Wl` prefix with PascalCase (e.g., `WlSchema`, `WlDiff`)
- Internal functions: `wl_` prefix with snake_case (e.g., `wl_diff_compare`)

## Compatibility

libwlite is compatible with SQLite 3.25 and later. It uses `ALTER TABLE ... RENAME COLUMN` when available (3.25+) and falls back to a rebuild for older versions. It uses `ALTER TABLE ... DROP COLUMN` when available (3.35+) and falls back to a rebuild for older versions.

The library is tested on Linux, macOS, and Windows. It uses no platform-specific APIs. The only system dependency is SQLite3. The C standard library functions used are limited to what is available in C99 and POSIX.

## Summary

libwlite is a focused C library that does one thing: bring a SQLite database into sync with a `.wlite` model file. It handles the hard cases (table rebuilds, constraint changes, type normalization) automatically. It exposes a clean C API that bindings can wrap in any language. It depends only on SQLite3 and the C standard library.

The architecture is designed for simplicity. Each module has a single responsibility. The data flow is linear and deterministic. There is no hidden state, no configuration files, and no side channels. The result is a library that is easy to understand, easy to integrate, and easy to trust.

For detailed information about each stage of the pipeline, see the linked documentation pages. For the full API reference, see the header file `include/wlite/wlite.h`.
