---
title: Architecture
description: libwlite internal architecture, data flow, and module design.
---

# libwlite Architecture

## Overview

libwlite is a single-layer C library. There is no CLI wrapper in this repository (the `wlite` CLI lives in the [wlite repo](https://github.com/dbwarden-org/wlite)). Everything here is the core engine: parsing, diffing, migration, and query execution.

## Data flow

```
   .wlite model file
         |
         v
   +-----------+         +-----------+
   |  parser   |-------->|  WlSchema |
   +-----------+         +-----+-----+
                                 |
                                 v
   +-----------+         +-----------+
   |introspect |<--------|    db     |
   +-----+-----+         +-----------+
         |
         v
   +-----------+
   |   diff    |
   +-----+-----+
         |
         v
   +-----------+
   |  planner  |
   +-----+-----+
         |
         v
   +-----------+
   |  migrate  |-------->  SQL statements
   +-----------+
```

1. **Parse**: `parser.c` reads a `.wlite` file into an in-memory `WlSchema` (tables, fields, constraints, indexes).
2. **Introspect**: `introspect.c` reads the live SQLite database schema into the same `WlSchema` format.
3. **Diff**: `diff.c` compares the two schemas and produces a `WlDiff` (list of additions, removals, and changes).
4. **Plan**: `planner.c` converts the diff into an ordered `WlPlan` of executable SQL statements.
5. **Migrate**: `migrate.c` executes the plan, records checksums, and verifies the result.

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
```

All bindings go through the C ABI. No binding reimplements libwlite semantics. Each binding wraps the same set of C functions: model load, database open, migrate, query, transactions.

## Core library modules

| Module | Purpose |
|--------|---------|
| `schema.c` | Schema lifecycle, database API, model API, memory management |
| `parser.c` | `.wlite` DSL parser (tokenizer + recursive descent) |
| `introspect.c` | Read live SQLite database schema into WlSchema |
| `diff.c` | Compare two WlSchema instances, produce WlDiff |
| `planner.c` | Convert WlDiff into ordered WlPlan (executable SQL) |
| `migrate.c` | Execute WlPlan, record checksums, verify result |
| `query.c` | Prepared statements, parameter binding |
| `record.c` | Generic row access (column-by-column or whole-row) |
| `tx.c` | Transactions and savepoints |
| `compile.c` | `.wlitem` compiled binary model format |
| `serialize.c` | JSON and DSL serialization of WlSchema |
| `schema_inspect.c` | Live DB to WlSchema bridge (used by introspect.c) |

## Migration internals

When `wlite_migrate` is called, the following happens:

1. The `.wlite` model is parsed into a `WlSchema`.
2. The live database is introspected into a second `WlSchema`.
3. The two schemas are diffed. Differences are classified as:
   - **Add**: new tables or columns to create
   - **Remove**: tables or columns to drop
   - **Alter**: column type, nullability, default, or comment changes
   - **Rebuild**: changes SQLite's `ALTER TABLE` cannot express (type changes, constraint changes, etc.)
4. The planner converts these into SQL statements in the correct order:
   - Foreign key dependencies are respected
   - Rebuilds generate `CREATE TABLE ... AS SELECT`, `DROP TABLE`, `ALTER TABLE RENAME`
   - Indexes and constraints are created after tables
5. The migration is executed within a transaction. If anything fails, the transaction is rolled back.

## SQLite-specific behavior

libwlite implements the same SQLite patterns as dbwarden:

- **Table rebuilds**: When a column type, nullability, or constraint changes in a way SQLite's `ALTER TABLE` cannot handle, libwlite rebuilds the table. It creates a new table with the correct schema, copies data, drops the old table, and renames.
- **Collapse logic**: Multiple consecutive rebuilds on the same table are collapsed into a single rebuild.
- **Type normalization**: SQLite types are normalized before comparison. `INT` and `INTEGER` are equivalent. `BOOLEAN` maps to `INTEGER`. `DATETIME` maps to `TEXT`.
- **Default handling**: Default values are compared semantically, not textually. `CURRENT_TIMESTAMP` and `current_timestamp` are the same.
- **Constraint diffing**: Primary keys, unique constraints, foreign keys, and check constraints are compared and diffed individually.

## Memory ownership

Objects returned by `_create`/`_load` functions belong to the caller.
Borrowed objects (e.g., `wlite_model_table`) are owned by their parent.

```
Caller owns:   wlite_db, wlite_model, wlite_stmt, wlite_tx
Library owns:  wlite_table, wlite_field (within model lifetime)
```

Always free what you create: `wlite_model_free`, `wlite_close`, `wlite_stmt_finalize`, `wlite_tx_free`.

## Error handling

All functions return `wlite_result`. Check with:

```c
wlite_result r = wlite_open("db", &db);
if (r != WLITE_OK) {
    fprintf(stderr, "Error: %s\n", wlite_strerror(r));
}
```

Error codes:

| Code | Meaning |
|------|---------|
| `WLITE_OK` | Success |
| `WLITE_ERROR` | General error |
| `WLITE_NOT_FOUND` | File or resource not found |
| `WLITE_MEMORY` | Allocation failed |
| `WLITE_IO` | I/O error |
| `WLITE_CORRUPT` | Corrupt data |
| `WLITE_RANGE` | Index out of range |

## Thread safety

- **Models** are immutable after loading and safe to share across threads.
- **Database connections** are not thread-safe. Use one connection per thread.
- **Statements** belong to a database connection. Do not share across threads.
