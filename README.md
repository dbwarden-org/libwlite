# libwlite

dbwarden's SQLite3 engine, extracted as a standalone C library.

## What It Does

libwlite parses `.wlite` model files, manages SQLite databases, compares schemas, generates migrations, and provides prepared statements with transactions and savepoints. It is the core runtime that powers the `wlite` CLI and all language bindings.

## Philosophy

libwlite exists because [dbwarden](https://github.com/dbwarden-org/dbwarden)'s SQLite3 backend is too useful to keep locked inside a Python project. The algorithms that make dbwarden's SQLite support production-grade (table rebuilds, collapse logic, type normalization, default handling, constraint diffing) are implemented here as a small, focused C library.

The principle is the same as dbwarden: **declare the schema you want, get the SQL to make it happen**. No migration scripts, no hidden behavior, no runtime magic.

## Relationship to dbwarden

libwlite is the SQLite3 engine from dbwarden, extracted as a standalone C library. A CI workflow checks that libwlite's behavior stays synchronized with dbwarden's SQLite backend. When dbwarden improves how it handles a type, default, or constraint, those improvements flow into libwlite automatically.

## Quick Start

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

## API

### Database

```c
wlite_result wlite_open(const char *path, wlite_db **out);
wlite_result wlite_open_ex(const char *path, const wlite_open_options *opts, wlite_db **out);
void wlite_close(wlite_db *db);
```

### SQL

```c
wlite_result wlite_execute(wlite_db *db, const char *sql, int64_t *rows_affected);
wlite_result wlite_prepare(wlite_db *db, const char *sql, wlite_stmt **out);
wlite_result wlite_bind_int64(wlite_stmt *stmt, int index, int64_t value);
wlite_result wlite_bind_double(wlite_stmt *stmt, int index, double value);
wlite_result wlite_bind_text(wlite_stmt *stmt, int index, const char *value);
wlite_result wlite_bind_null(wlite_stmt *stmt, int index);
wlite_result wlite_step(wlite_stmt *stmt);
void wlite_stmt_finalize(wlite_stmt *stmt);
```

### Model

```c
wlite_result wlite_model_load_file(const char *path, wlite_model **out);
wlite_result wlite_model_load_memory(const void *data, size_t size, wlite_model **out);
wlite_result wlite_model_load_compiled(const void *data, size_t size, wlite_model **out);
wlite_result wlite_model_validate(const wlite_model *model);
void wlite_model_free(wlite_model *model);
```

### Transactions and Savepoints

```c
wlite_result wlite_begin(wlite_db *db, wlite_tx **out);
wlite_result wlite_commit(wlite_tx *tx);
wlite_result wlite_rollback(wlite_tx *tx);
void wlite_tx_free(wlite_tx *tx);
wlite_result wlite_savepoint(wlite_tx *tx, const char *name);
wlite_result wlite_release(wlite_tx *tx, const char *name);
wlite_result wlite_rollback_to(wlite_tx *tx, const char *name);
```

### Migration

```c
wlite_result wlite_diff(wlite_db *db, const wlite_model *model, WlPlan **out);
wlite_result wlite_migrate(wlite_db *db, const wlite_model *model);
size_t wlite_plan_count(const WlPlan *plan);
```

### Error Handling

```c
const char *wlite_strerror(wlite_result result);
void wlite_error_free(wlite_error *err);
```

## Memory Rules

Caller owns: `wlite_db`, `wlite_model`, `wlite_stmt`, `wlite_tx`.
Library owns: `wlite_table`, `wlite_field` (freed when model is freed).

## Thread Safety

Models are immutable after loading and safe to share. Database connections are not thread-safe. Use one connection per thread.

## License

MIT
