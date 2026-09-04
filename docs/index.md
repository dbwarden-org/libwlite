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

libwlite parses `.wlite` model files, manages SQLite databases, compares schemas,
generates migrations, and provides prepared statements with transactions and
savepoints. It is the core runtime that powers the `wlite` CLI and all language
bindings.

## Why libwlite exists

[dbwarden](https://github.com/dbwarden-org/dbwarden) is a full-featured
declarative schema compiler for Python and SQLAlchemy. Its SQLite3 backend handles
the hard parts: table rebuilds, type normalization, collapse logic, default
handling, and constraint diffing.

But dbwarden is a Python project. It requires a Python runtime, pip, and the
entire Python ecosystem to function. Embedded applications, CLI tools, TUI
interfaces, and small C/C++ services cannot use it directly. They need a library
written in a systems language, with no runtime dependencies beyond the operating
system.

**libwlite is dbwarden's SQLite3 engine, without the Python.**

### Standalone C library use cases

libwlite fills a gap that dbwarden, by design, does not attempt to address.

**Embedded applications.** Many devices and appliances store data in SQLite but
have no room for a Python runtime. A medical device, an industrial controller,
or an IoT gateway can use libwlite to manage its database schema without pulling
in megabytes of interpreted code. The library links against a single shared
object and adds only a few hundred kilobytes to the binary.

**CLI tools and developer utilities.** Command line programs that ship as
standalone binaries benefit from a schema engine they can link directly. A
database inspection tool, a migration runner, or a data pipeline written in C or
C++ can call libwlite to parse model files, diff live schemas, and apply
migrations without spawning a Python process.

**TUI and terminal interfaces.** Terminal UIs built with ncurses, libvterm, or
similar libraries often need database access. Linking libwlite gives them full
schema management without leaving the terminal process or shelling out to an
external command.

**Small services and daemons.** Lightweight network services written in C, Rust
with FFI, or Go with cgo may need embedded SQLite with proper schema management.
libwlite lets those services define their schema declaratively and keep the
database in sync at startup, without a separate migration step.

**Scripting language bindings.** The wlite CLI and language bindings (Ruby,
Node.js, others) use libwlite as their backend. Instead of reimplementing
dbwarden's schema logic in every language, a binding links to libwlite and gets
the same behavior dbwarden provides in Python.

**Testing and CI.** Test suites that need real SQLite databases with specific
schemas can use libwlite to create and migrate those databases quickly. The
library starts in milliseconds, requires no virtual environment, and cleans up
predictably.

## Philosophy

The principle is the same as dbwarden: **declare the schema you want, get the
SQL to make it happen**. No migration scripts, no hidden behavior, no runtime
magic. Just a C library that knows how to reconcile a `.wlite` model with a live
SQLite database.

libwlite exists because schema management should not be tied to a single
language or runtime. A well-designed schema engine is a universal tool. It should
work in Python and in C, in a web application and on an embedded device, through
a rich ORM and through a plain compiled binary.

The library follows these principles:

- **Declarative over imperative.** You describe the target state. libwlite
  computes the steps to reach it.
- **Explicit over implicit.** Every migration is visible. Nothing happens behind
  your back.
- **Small over large.** The library does one thing: SQLite schema management.
  It does not manage connections to PostgreSQL, MySQL, or other engines.
- **Portable over platform-specific.** libwlite compiles on Linux, macOS,
  FreeBSD, and Windows with standard toolchains. It uses POSIX and Win32 APIs
  where needed but avoids platform-specific extensions.
- **Testable over monolithic.** The test suite exercises every public API
  function. The same tests run in CI against dbwarden's SQLite backend to
  ensure behavioral parity.

A CI workflow keeps libwlite's behavior synchronized with dbwarden's SQLite
backend. When dbwarden improves how it handles a type, default, or constraint,
those improvements flow into libwlite automatically. This is not a one-time
extraction. It is an ongoing synchronization.

## At a glance

- Parse `.wlite` model files into an in-memory schema representation
- Open, create, and manage SQLite databases
- Compare live database schemas against model definitions
- Generate SQL migration statements from schema diffs
- Execute migrations automatically or review them first
- Prepared statements with parameter binding (positional and named)
- Transactions with explicit begin, commit, and rollback
- Nested savepoints for partial rollback within transactions
- Schema snapshots and content hashing for change detection
- Foreign key enforcement and pragma management
- Table rebuild with data preservation during destructive changes
- Type normalization to SQLite's affinity rules
- Default value handling, including expressions and NULL
- Collapse logic for columns that can be modified in place
- Constraint diffing for CHECK, UNIQUE, and foreign keys
- Small, focused C library (C11), zero dependencies beyond SQLite3
- MIT licensed, suitable for inclusion in proprietary projects

## Why libwlite

There are other ways to manage SQLite schemas from C. libwlite is different in
several important ways.

### Compared to hand-written SQL migrations

Writing migration SQL by hand works for simple projects. As schemas grow, the
migrations become complex. Adding a column to a table with indexes, triggers,
and views requires careful sequencing. Renaming a column without losing data
requires a multi-step process. libwlite handles these cases automatically,
correctly, and repeatedly.

Hand-written migrations also tend to accumulate inconsistencies. Two developers
may write different migrations for the same schema change. A deployment may skip
a migration. libwlite always starts from the model and the current database
state, so the result is deterministic.

### Compared to ORMs that auto-generate migrations

ORMs like SQLAlchemy can generate migration scripts. But they require the ORM's
runtime. A C project cannot use SQLAlchemy. Even within Python, auto-generated
migrations sometimes produce unexpected results when columns are reordered or
defaults change. libwlite operates at the schema level, not the ORM level, and
its diff algorithm is designed specifically for SQLite's type system and
constraints.

### Compared to raw SQLite3 API usage

The SQLite3 C API is powerful but low-level. It provides the building blocks:
`sqlite3_exec`, `sqlite3_prepare_v2`, `sqlite3_step`, and so on. But it does not
provide schema comparison, migration generation, or model parsing. You would need
to write all of that yourself. libwlite wraps these higher-level operations into
a coherent API while still giving you access to the underlying SQLite3 handles
when you need them.

### Compared to other schema migration libraries

Most schema migration tools are written for specific languages or frameworks.
They assume a runtime, a package manager, or a particular project structure.
libwlite is a plain C library. It links with a single `-lwlite` flag. It works
in any C or C++ project, any build system, and any deployment environment.

### What libwlite is not

libwlite is not a general-purpose database abstraction layer. It does not connect
to PostgreSQL, MySQL, or other database engines. It does not provide an ORM, a
query builder, or a connection pool. It manages SQLite schemas, and it does that
one thing well.

If you need multi-database support, use dbwarden. If you need a C library for
SQLite schema management, use libwlite.

## Quick start

The following example loads a `.wlite` model, opens or creates a database,
applies any pending migrations, and then queries the result. Every step is
explicit and every resource is cleaned up.

```c
#include <stdio.h>
#include <stdlib.h>
#include <wlite/wlite.h>

int main(void) {
    wlite_model *model = NULL;
    wlite_db   *db    = NULL;
    wlite_stmt *stmt  = NULL;
    int         rc;

    /*
     * Load the .wlite model file.
     * The model contains the desired schema: tables, columns,
     * types, defaults, indexes, and constraints.
     */
    rc = wlite_model_load_file("app.wlite", &model);
    if (rc != WLITE_OK) {
        fprintf(stderr, "failed to load model: %d\n", rc);
        return EXIT_FAILURE;
    }

    /*
     * Open (or create) the SQLite database.
     * If the file does not exist, sqlite3 creates it.
     * If it exists, sqlite3 opens it.
     */
    rc = wlite_open("app.db", &db);
    if (rc != WLITE_OK) {
        fprintf(stderr, "failed to open database: %d\n", rc);
        wlite_model_free(model);
        return EXIT_FAILURE;
    }

    /*
     * Migrate the database to match the model.
     * This compares the live schema against the model,
     * computes the diff, and applies the necessary SQL.
     * If the database already matches, nothing happens.
     */
    rc = wlite_migrate(db, model);
    if (rc != WLITE_OK) {
        fprintf(stderr, "migration failed: %d\n", rc);
        wlite_close(db);
        wlite_model_free(model);
        return EXIT_FAILURE;
    }

    printf("database migrated successfully\n");

    /*
     * Prepare a SELECT statement and iterate over results.
     * wlite_prepare wraps sqlite3_prepare_v2 and gives you
     * a wlite_stmt handle for use with wlite_step and
     * wlite_column_* functions.
     */
    rc = wlite_prepare(db, "SELECT id, name FROM users", &stmt);
    if (rc != WLITE_OK) {
        fprintf(stderr, "prepare failed: %d\n", rc);
        wlite_close(db);
        wlite_model_free(model);
        return EXIT_FAILURE;
    }

    while (wlite_step(stmt) == WLITE_OK) {
        int    id   = wlite_column_int64(stmt, 0);
        const char *name = wlite_column_text(stmt, 1);
        printf("user %d: %s\n", id, name);
    }

    /*
     * Finalize the statement to release resources.
     * Always finalize before closing the database.
     */
    wlite_stmt_finalize(stmt);

    /*
     * Close the database and free the model.
     * Order matters: close the database before freeing
     * the model, because the model may reference
     * database-specific metadata.
     */
    wlite_close(db);
    wlite_model_free(model);

    return EXIT_SUCCESS;
}
```

Save this file as `main.c`. The model file `app.wlite` should contain your
schema definition. A minimal example:

```
model User {
    table "users"

    field id integer {
        primary_key
        autoincrement
    }

    field name text {
        not_null
    }

    field email text {
        unique
    }

    field created_at datetime {
        not_null
        default CURRENT_TIMESTAMP
    }
}
```

## Build

libwlite supports two build systems: Make (for quick builds and development)
and CMake (for integration into larger projects and cross-platform builds).

### Requirements

- A C11 compiler (GCC, Clang, or MSVC)
- SQLite3 development library (`libsqlite3-dev` on Debian/Ubuntu,
  `sqlite-devel` on Fedora/RHEL, `sqlite3` via Homebrew on macOS)
- GNU Make or CMake 3.14 or newer

### Make

The Makefile is designed for POSIX systems (Linux, macOS, BSD). It produces both
a static archive and a shared library.

```bash
# Build the static and shared libraries
make

# Build and run the test suite
make test
```

The Makefile produces:

- `libwlite.a` - static archive for linking into executables
- `libwlite.so` - shared library for dynamic linking

Running `make test` compiles and executes the full test suite. Tests cover model
parsing, schema comparison, migration generation, prepared statements,
transactions, savepoints, and error handling.

### CMake

CMake is the recommended build system for projects that embed libwlite as a
dependency. It handles platform detection, compiler flags, and install rules
automatically.

```bash
# Configure the build
mkdir build && cd build
cmake ..

# Build the libraries and CLI
cmake --build .

# Run the test suite
ctest

# Build with specific options
cmake -DWLITE_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
```

CMake produces:

- `libwlite.a` - static archive
- `libwlite.so.0.2.0` - shared library with SOVERSION 0
- `wlite` - the command line interface tool (when WLITE_BUILD_CLI is ON)
- `wlite.pc` - pkg-config file for dependency detection

The CMake build supports the following options:

- `WLITE_BUILD_SHARED` - build the shared library (default: ON)
- `WLITE_BUILD_STATIC` - build the static library (default: ON)
- `WLITE_BUILD_CLI` - build the wlite CLI tool (default: ON)
- `WLITE_BUILD_TESTS` - build the test suite (default: ON)
- `CMAKE_BUILD_TYPE` - Release, Debug, RelWithDebInfo, MinSizeRel
- `CMAKE_INSTALL_PREFIX` - installation prefix (default: /usr/local)

## Installation

After building, libwlite can be installed system-wide or locally for use by
other projects.

### Make install

```bash
# Install to /usr/local (default prefix)
make install

# Install to a custom prefix
make install PREFIX=/opt/libwlite

# Install to a staging directory for packaging
make install DESTDIR=/tmp/staging
```

The Make install copies:

- `libwlite.a` and `libwlite.so` to `$PREFIX/lib/`
- Header files to `$PREFIX/include/wlite/`

### CMake install

```bash
mkdir build && cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
cmake --build .
sudo cmake --install .
```

The CMake install copies:

- `libwlite.a` and `libwlite.so.0.2.0` to `$PREFIX/lib/`
- Headers to `$PREFIX/include/wlite/`
- The `wlite` CLI to `$PREFIX/bin/` (when WLITE_BUILD_CLI is ON)
- `wlite.pc` to `$PREFIX/lib/pkgconfig/`

### pkg-config

After installation, other projects can find libwlite using pkg-config:

```bash
# Check if libwlite is installed
pkg-config --modversion wlite

# Get compiler flags
pkg-config --cflags wlite

# Get linker flags
pkg-config --libs wlite
```

In a Makefile:

```makefile
CFLAGS  += $(shell pkg-config --cflags wlite)
LDFLAGS += $(shell pkg-config --libs wlite)
```

In a CMakeLists.txt:

```cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(WLITE REQUIRED wlite)
target_include_directories(myapp PRIVATE ${WLITE_INCLUDE_DIRS})
target_link_libraries(myapp ${WLITE_LINK_LIBRARIES})
```

## Relationship to dbwarden

dbwarden is a declarative schema compiler for Python and SQLAlchemy. It is
feature-rich: multi-database support (SQLite, PostgreSQL, MySQL), plugin systems,
async drivers, seed management, and more. It is the reference implementation of
the `.wlite` grammar and the schema comparison algorithm.

libwlite is what happens when you take dbwarden's SQLite3 engine and remove
everything except SQLite. The table rebuild algorithms, type normalization,
collapse logic, and constraint diffing that make dbwarden's SQLite support
production-grade live here.

### What libwlite inherits from dbwarden

The core schema comparison algorithm is shared between dbwarden and libwlite.
This means:

- **Type normalization.** SQLite has flexible type affinity rules. Both
  dbwarden and libwlite normalize types the same way, so a column defined as
  `VARCHAR(255)` in the model matches a column stored as `TEXT` in the database.
- **Default handling.** Default values, including expressions like
  `current_timestamp` and `datetime('now')`, are compared correctly. Expressions
  that are semantically equivalent but syntactically different are recognized.
- **Collapse logic.** Columns that differ only in ways SQLite can handle with
  `ALTER TABLE` (adding a column, dropping a column) are modified in place
  rather than triggering a full table rebuild.
- **Table rebuild.** When a column change requires a table rebuild (type change,
  constraint change, reorder), libwlite performs the rebuild with data
  preservation. The old table is renamed, a new table is created, data is
  copied, indexes and triggers are recreated, and the old table is dropped.
- **Constraint diffing.** CHECK constraints, UNIQUE constraints, and foreign key
  constraints are compared and migrated correctly.

### Behavioral parity

A CI workflow runs the same test scenarios against both dbwarden's SQLite
backend and libwlite. When dbwarden improves how it handles a type, default, or
constraint, those improvements flow into libwlite automatically. This is not a
one-time extraction. It is an ongoing synchronization that ensures behavioral
parity.

If you find a case where libwlite and dbwarden disagree, that is a bug. Please
report it.

### The difference in scope

dbwarden supports multiple database engines. libwlite supports SQLite only. This
is a deliberate constraint. By focusing on a single engine, libwlite can be
small, fast, and predictable. It does not need to abstract away differences
between database systems. It knows exactly what SQLite can and cannot do.

## Next steps

- Read the [Architecture](architecture/index.md) overview to understand how libwlite
  is organized internally
- Learn the [.wlite Grammar](grammar.md) to write model files
- Browse the [C API Reference](c-api.md) for the complete list of functions and
  types
- Understand [SQLite Patterns](architecture/sqlite-patterns.md) for rebuilds and
  type normalization
- Learn about [Memory and Errors](architecture/memory-and-errors.md) for correct
  usage from any language
