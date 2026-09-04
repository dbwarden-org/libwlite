---
title: libwlite - C Library for SQLite Schema Management
description: A small C library for SQLite schema management, migrations, and queries. Core runtime for wlite.
---

<p align="center">
  <strong style="font-size: 2.5em;">libwlite</strong>
</p>
<p align="center">
  <em>A small C library for SQLite schema management, migrations, and queries.</em>
</p>
<p align="center">
  <a href="https://github.com/dbwarden-org/libwlite/blob/main/LICENSE">
    <img src="https://img.shields.io/badge/License-MIT-10AC84?style=for-the-badge" alt="License">
  </a>
  <a href="https://github.com/dbwarden-org/libwlite">
    <img src="https://img.shields.io/badge/GitHub-dbwarden--org%2Flibwlite-181717?logo=github&style=for-the-badge" alt="GitHub">
  </a>
</p>

---

libwlite parses `.wlite` model files, manages SQLite databases, compares schemas, generates migrations, and provides prepared statements with transactions and savepoints. It is the core runtime that powers the `wlite` CLI and all language bindings.

## At a glance

- Parse `.wlite` model files into an in-memory schema
- Open and manage SQLite databases
- Compare live database schemas against models
- Generate and execute migrations
- Prepared statements with parameter binding
- Transactions and savepoints
- Schema snapshots and hashing
- Small, focused C library (C11)

## Why libwlite

SQLite has no built-in schema management. Most tools layer imperative migration scripts on top. libwlite takes a declarative approach: define the desired state in a `.wlite` model, and libwlite computes the diff and generates the SQL to reconcile it.

- No migration runtime: plain SQL output
- Mirrors [dbwarden](https://github.com/dbwarden-org/dbwarden)'s SQLite backend patterns
- Powers the `wlite` CLI and bindings for C++, Rust, Python, Go, and Zig
- Thread-safe models (immutable after loading)
- 70+ tests covering conformance with dbwarden's SQLite behavior

## Quick start

```c
#include <wlite/wlite.h>

int main(void) {
    wlite_model *model = NULL;
    wlite_db *db = NULL;

    wlite_model_load_file("app.wlite", &model);
    wlite_open("app.db", &db);
    wlite_migrate(db, model);

    wlite_stmt *stmt;
    wlite_prepare(db, "SELECT * FROM users", &stmt);
    while (wlite_step(stmt) == WLITE_OK)
        printf("%s\n", wlite_column_text(stmt, 0));
    wlite_stmt_finalize(stmt);

    wlite_close(db);
    wlite_model_free(model);
    return 0;
}
```

## Build

```bash
make              # builds libwlite.a and libwlite.so
make test         # runs 70 tests
make install      # installs to /usr/local
```

Requires: C11 compiler, SQLite3 development library.

## Relationship to dbwarden

libwlite follows [dbwarden](https://github.com/dbwarden-org/dbwarden) and mirrors its SQLite3 backend patterns: table rebuilds, collapse logic, type normalization, default handling, and constraint diffing. The dbwarden SQLite backend is the reference implementation. A CI workflow enforces that libwlite's SQLite behavior stays synchronized with dbwarden.

## Next steps

- Read the [Architecture](architecture.md) overview
- Browse the [C API Reference](c-api.md)
- Learn the [.wlite Grammar](grammar.md)
