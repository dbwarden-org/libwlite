---
title: libwlite - C Library for SQLite Schema Management
description: A lightweight C library implementing dbwarden's SQLite3 patterns. Schema management, migrations, and queries for embedded and CLI projects.
---

<p align="center">
  <strong style="font-size: 2.5em;">libwlite</strong>
</p>
<p align="center">
  <em>dbwarden's SQLite3 engine, extracted as a standalone C library.</em>
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

## Why libwlite exists

[dbwarden](https://github.com/dbwarden-org/dbwarden) is a full-featured declarative schema compiler for Python and SQLAlchemy. Its SQLite3 backend handles the hard parts: table rebuilds, type normalization, collapse logic, default handling, and constraint diffing.

But dbwarden is a Python project. Embedded applications, CLI tools, TUI interfaces, and small C/C++ services cannot use it directly. libwlite takes dbwarden's SQLite3 engine and makes it available as a standalone C library.

**libwlite is dbwarden's SQLite3 engine, without the Python.**

## Philosophy

The principle is the same as dbwarden: **declare the schema you want, get the SQL to make it happen**. No migration scripts, no hidden behavior, no runtime magic. Just a C library that knows how to reconcile a `.wlite` model with a live SQLite database.

A CI workflow keeps libwlite's behavior synchronized with dbwarden's SQLite backend. When dbwarden improves how it handles a type, default, or constraint, those improvements flow into libwlite automatically.

## At a glance

- Parse `.wlite` model files into an in-memory schema
- Open and manage SQLite databases
- Compare live database schemas against models
- Generate and execute migrations
- Prepared statements with parameter binding
- Transactions and savepoints
- Schema snapshots and hashing
- Small, focused C library (C11), zero dependencies beyond SQLite3

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

### CMake

```bash
mkdir build && cd build
cmake ..
cmake --build .
ctest
cmake --install .
```

CMake produces `libwlite.a`, `libwlite.so.0.2.0` (with SOVERSION 0), the `wlite` CLI, and a `wlite.pc` pkg-config file.

## Relationship to dbwarden

dbwarden is a declarative schema compiler for Python and SQLAlchemy. It is feature-rich: multi-database support, plugin systems, async drivers, seed management, and more.

libwlite is what happens when you take dbwarden's SQLite3 engine and remove everything except SQLite. The table rebuild algorithms, type normalization, collapse logic, and constraint diffing that make dbwarden's SQLite support production-grade live here.

A CI workflow keeps libwlite's behavior synchronized with dbwarden's SQLite backend. When dbwarden improves how it handles a type, default, or constraint, those improvements flow into libwlite automatically.

The result: a C library that carries dbwarden's quality without dbwarden's weight.

## Next steps

- Read the [Architecture](architecture.md) overview
- Learn the [.wlite Grammar](grammar.md)
- Browse the [C API Reference](c-api.md)
