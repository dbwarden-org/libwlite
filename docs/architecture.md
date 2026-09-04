---
title: Architecture
description: libwlite internal architecture and module design.
---

# libwlite Architecture

## Components

```
                    *.wlite
                       │
                       ▼
                  ┌─────────┐
                  │  wlite  │
                  │   CLI   │
                  └────┬────┘
                       │
                  model / schema
                       │
                       ▼
                  ┌─────────┐
                  │libwlite │
                  │    C    │
                  └────┬────┘
                       │
                    SQLite3
```

## Language Bindings

```
libwlite C ABI
      │
      ├── C/C++ (direct)
      ├── Rust  (FFI)
      ├── Python (ctypes)
      ├── Go (cgo)
      └── Zig (@cImport)
```

All bindings go through the C ABI. No binding reimplements libwlite semantics.

## Core Library Modules

| Module | Purpose |
|--------|---------|
| `schema.c` | Schema lifecycle, database API, model API |
| `parser.c` | .wlite DSL parser |
| `introspect.c` | SQLite database introspection |
| `diff.c` | Schema comparison engine |
| `planner.c` | Migration plan generation |
| `migrate.c` | Migration execution, checksums, verification |
| `query.c` | Prepared statements, parameter binding |
| `record.c` | Generic record access |
| `tx.c` | Transactions and savepoints |
| `compile.c` | .wlitem compiled model format |
| `serialize.c` | JSON/DSL serialization |
| `schema_inspect.c` | Live DB to WlSchema bridge |

## Memory Ownership

Objects returned by `_create`/`_load` functions belong to the caller.
Borrowed objects (e.g., `wlite_model_table`) are owned by their parent.

```
Caller owns:   wlite_db, wlite_model, wlite_stmt, wlite_tx
Library owns:  wlite_table, wlite_field (within model lifetime)
```

## Error Handling

All functions return `wlite_result`. Check with:

```c
wlite_result r = wlite_open("db", &db);
if (r != WLITE_OK) {
    fprintf(stderr, "Error: %s\n", wlite_strerror(r));
}
```

## Thread Safety

Models are immutable after loading and safe to share across threads (e.g., via `shared_ptr` in C++ or `Arc` in Rust). Database connections are not thread-safe; use one connection per thread.
