---
title: Data Flow
description: The complete pipeline from .wlite model to executable SQL.
---

# Data Flow

This page walks through the complete pipeline from a `.wlite` model file to executable SQL. Every call to `wlite_migrate` or `wlite_diff` goes through these stages.

## Stage 1: Parse

The parser reads a `.wlite` file and produces an in-memory `WlSchema`. The schema contains tables, columns, check constraints, unique constraints, foreign keys, indexes, views, and triggers.

```
.wlite file  --->  parser.c  --->  WlSchema
```

### What the parser handles

- Tokenization: keywords, identifiers, strings, numbers, operators
- Recursive descent: model blocks, field declarations, constraints, indexes
- Comment stripping: line comments (`#`) and block comments (`/* */`)
- Error reporting: line number and column for syntax errors

The parser is a hand-written recursive descent parser. It does not use lex or yacc. This keeps the dependency count at zero and makes error messages clear.

### WlSchema structure

```c
typedef struct WlSchema {
    uint32_t version;            // schema version number
    WlTable *tables;             // array of tables
    size_t table_count;          // number of tables
    WlIndex *indexes;            // array of indexes
    size_t index_count;          // number of indexes
    WlView *views;               // array of views
    size_t view_count;           // number of views
    WlTrigger *triggers;         // array of triggers
    size_t trigger_count;        // number of triggers
    char *model_name;            // model name string
    int model_version;           // model version integer
} WlSchema;
```

Each `WlTable` contains an array of `WlColumn` (columns), an array of `WlCheck` (check constraints), an array of `WlUnique` (unique constraints), and an array of `WlForeignKey` (foreign key constraints).

### Example

Given this `.wlite` file:

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
}
```

The parser produces a `WlSchema` with:

- 1 table: `users`
- 2 columns: `id` (INTEGER, PK, AUTOINCREMENT), `name` (TEXT, NOT NULL)
- 0 indexes, 0 views, 0 triggers

### Parser error messages

When the parser encounters a syntax error, it returns a detailed error with the line number, column, and a description of what was expected. This makes it easy to locate and fix problems in model files.

```
Error at line 12, column 5: expected 'field' or '}', found 'clas'
```

The parser does not attempt to recover from errors. The first syntax error stops parsing and returns an error code. This is a deliberate choice: partial schemas are dangerous because they can produce unexpected migrations.

## Stage 2: Introspect

The introspector reads the live SQLite database and produces a `WlSchema` in the same format as the parser output.

```
SQLite DB  --->  introspect.c  --->  WlSchema
```

### What the introspector reads

- `sqlite_master` for table definitions
- `PRAGMA table_info()` for column details (name, type, nullable, default)
- `PRAGMA index_list()` and `PRAGMA index_info()` for index details
- `PRAGMA foreign_key_list()` for foreign key details
- `PRAGMA table_xinfo()` for generated columns

The introspector handles all SQLite quirks. It normalizes type strings, resolves default value expressions, and detects the various ways SQLite encodes primary keys, autoincrement, and generated columns.

### Type normalization

SQLite stores types as strings. The introspector normalizes them for comparison:

| Stored type | Normalized to |
|-------------|---------------|
| `INT`, `INTEGER`, `INT4`, `BIGINT`, `SIGNED` | `INTEGER` |
| `BOOLEAN`, `BOOL`, `TINYINT` | `INTEGER` |
| `REAL`, `DOUBLE`, `FLOAT`, `NUMERIC` | `REAL` |
| `TEXT`, `VARCHAR`, `CHAR`, `CLOB` | `TEXT` |
| `DATETIME`, `TIMESTAMP` | `TEXT` |
| `BLOB` | `BLOB` |

This normalization means changing `INT` to `INTEGER` in your model does not trigger a migration.

### Constraint detection

The introspector reads constraints from multiple sources:

- Primary keys from `PRAGMA table_info()` (the `pk` column)
- Unique constraints from `PRAGMA index_list()` (the `unique` flag)
- Foreign keys from `PRAGMA foreign_key_list()`
- Check constraints parsed from the `CREATE TABLE` SQL in `sqlite_master`

This is necessary because SQLite does not have a single pragma that returns all constraints. The introspector assembles them from several sources.

### Example

If the live database has:

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    email TEXT
);
```

The introspector produces a `WlSchema` with:

- 1 table: `users`
- 3 columns: `id` (INTEGER, PK, AUTOINCREMENT), `name` (TEXT, NOT NULL), `email` (TEXT, nullable)

## Stage 3: Diff

The diff engine compares the model schema against the database schema and produces a `WlDiff`.

```
WlSchema (model)  +  WlSchema (db)  --->  diff.c  --->  WlDiff
```

### Diff operations

| Operation | Meaning |
|-----------|---------|
| `WL_DIFF_ADD_TABLE` | New table to create |
| `WL_DIFF_DROP_TABLE` | Table to remove |
| `WL_DIFF_RENAME_TABLE` | Table to rename |
| `WL_DIFF_ADD_COLUMN` | New column to add |
| `WL_DIFF_DROP_COLUMN` | Column to remove |
| `WL_DIFF_RENAME_COLUMN` | Column to rename |
| `WL_DIFF_ALTER_COLUMN` | Column type, nullability, default, or comment change |
| `WL_DIFF_ADD_INDEX` | New index to create |
| `WL_DIFF_DROP_INDEX` | Index to remove |
| `WL_DIFF_ALTER_INDEX` | Index definition change |
| `WL_DIFF_ADD_CHECK` | New check constraint to add |
| `WL_DIFF_DROP_CHECK` | Check constraint to remove |
| `WL_DIFF_ADD_UNIQUE` | New unique constraint to add |
| `WL_DIFF_DROP_UNIQUE` | Unique constraint to remove |
| `WL_DIFF_ADD_FKEY` | New foreign key to add |
| `WL_DIFF_DROP_FKEY` | Foreign key to remove |
| `WL_DIFF_ALTER_TABLE_OPTIONS` | Table options change (STRICT, WITHOUT ROWID) |
| `WL_DIFF_ALTER_VIEW` | View definition change |
| `WL_DIFF_ALTER_TRIGGER` | Trigger definition change |
| `WL_DIFF_REBUILD_TABLE` | Table requires full rebuild |

### Classification

Each difference is classified by severity:

- **Additive**: new tables, columns, indexes (safe, non-destructive)
- **Subtractive**: dropping tables or columns (destructive, requires confirmation)
- **Alterative**: changing column properties (may require rebuild)
- **Rebuild**: changes SQLite's ALTER TABLE cannot express

The classification drives both the plan output and the optional confirmation step. Additive changes can be applied silently. Subtractive changes should always be reviewed.

### Column comparison

Columns are compared on five properties:

1. **Type**: after normalization, types must match
2. **Nullability**: NULL vs NOT NULL
3. **Default**: semantically equivalent defaults are treated as equal
4. **Generated**: stored vs virtual, and the generation expression
5. **Comment**: optional comment text

If any property differs, the column is marked as altered. The planner decides whether the alteration can be done with ALTER TABLE or requires a rebuild.

### Constraint comparison

Constraints are compared by their structural properties:

- Primary keys: column list and sort order
- Unique constraints (`WlUnique`): column list
- Foreign keys (`WlForeignKey`): target table, target column, ON DELETE, ON UPDATE
- Check constraints (`WlCheck`): expression text

Any change to a constraint triggers a rebuild. There is no way to ALTER a constraint in SQLite without rebuilding the table.

### Example

Model has: `id`, `name`, `email`, `created_at`
Database has: `id`, `name`, `email`

Diff result:
- `WL_DIFF_ADD_COLUMN`: `created_at` (DATETIME, NOT NULL, DEFAULT CURRENT_TIMESTAMP)

## Stage 4: Plan

The planner converts the diff into an ordered `WlPlan` of executable SQL statements.

```
WlDiff  --->  planner.c  --->  WlPlan
```

### Planning rules

1. **Ordering**: Tables are created before columns are added. Indexes are created after tables.
2. **Dependencies**: Foreign key targets are created before referencing tables.
3. **Rebuilds**: Table rebuilds are expanded into a sequence: create staging, copy data, drop old, rename.
4. **Collapse**: Multiple rebuilds on the same table are collapsed into one.
5. **Views and triggers**: Created after all tables and indexes exist.

### WlPlan structure

```c
typedef struct WlPlan {
    WlPlanStep *steps;              // array of SQL statements
    size_t step_count;              // number of steps
    char *schema_hash_before;       // FNV-1a hash of schema before migration
    char *schema_hash_after;        // FNV-1a hash of schema after migration
} WlPlan;
```

Each `WlPlanStep` contains a SQL string, a rollback SQL string, the target table name, a detail description, a safety classification, and an `is_non_atomic` flag.

### Step types

| Step type | Purpose |
|-----------|---------|
| `WL_PLAN_CREATE_TABLE` | Create a new table |
| `WL_PLAN_DROP_TABLE` | Drop an existing table |
| `WL_PLAN_RENAME_TABLE` | Rename a table |
| `WL_PLAN_ADD_COLUMN` | Add a column to an existing table |
| `WL_PLAN_DROP_COLUMN` | Remove a column from a table |
| `WL_PLAN_RENAME_COLUMN` | Rename a column |
| `WL_PLAN_ALTER_COLUMN` | Change a column property |
| `WL_PLAN_REBUILD_TABLE` | Part of a table rebuild sequence |
| `WL_PLAN_CREATE_INDEX` | Create a new index |
| `WL_PLAN_DROP_INDEX` | Drop an existing index |
| `WL_PLAN_ADD_CHECK` | Add a check constraint (via rebuild) |
| `WL_PLAN_DROP_CHECK` | Drop a check constraint (via rebuild) |
| `WL_PLAN_ADD_UNIQUE` | Add a unique constraint (via rebuild) |
| `WL_PLAN_DROP_UNIQUE` | Drop a unique constraint (via rebuild) |
| `WL_PLAN_ADD_FKEY` | Add a foreign key (via rebuild) |
| `WL_PLAN_DROP_FKEY` | Drop a foreign key (via rebuild) |
| `WL_PLAN_CUSTOM_SQL` | Execute custom SQL |

### Example

For the diff above (adding `created_at`), the plan is:

```sql
ALTER TABLE users ADD COLUMN created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP;
```

One step. No rebuild needed.

## Stage 5: Migrate

The migration engine executes the plan within a transaction.

```
WlPlan  --->  migrate.c  --->  SQL executed, checksum recorded
```

### What migrate does

1. Begin a transaction
2. Execute each step in order
3. After each step, verify the result (optional, controlled by options)
4. Record a checksum of the final schema state
5. Commit the transaction (or rollback on error)

### Checksums

After migration, libwlite computes an FNV-1a hash of the schema state. This hash is stored in the plan's `schema_hash_after` field and is used by `wl_schema_verify` to verify that the database matches the model without re-introspecting.

### Error handling

If any step fails, the entire migration is rolled back. The error includes the failing SQL statement and the SQLite error message.

### Dry run

You can call `wlite_diff` instead of `wlite_migrate` to see the plan without executing it. This is useful for reviewing changes before applying them.

```c
WlPlan *plan = NULL;
wlite_result r = wlite_diff(db, model, &plan);
if (r == WLITE_OK) {
    for (size_t i = 0; i < wlite_plan_count(plan); i++) {
        printf("%s\n", plan->steps[i].sql);
    }
}
wl_plan_free(plan);
```

## Complete example

```c
#include <wlite/wlite.h>

int main(void) {
    wlite_model *model = NULL;
    wlite_db *db = NULL;

    // Stage 1: Load model
    wlite_model_load_file("schema.wlite", &model);

    // Stage 2-5: Open DB and migrate
    wlite_open("app.db", &db);
    wlite_migrate(db, model);

    // Or just preview (stages 2-4 only)
    WlPlan *plan = NULL;
    wlite_diff(db, model, &plan);
    printf("Migration has %zu steps\n", wlite_plan_count(plan));

    wl_plan_free(plan);
    wlite_close(db);
    wlite_model_free(model);
    return 0;
}
```

## Error paths

Every stage can return an error. The error codes are documented in [Memory and Errors](memory-and-errors.md). The most common errors are:

- `WLITE_NOT_FOUND`: the model file or database does not exist
- `WLITE_MODEL_ERROR`: the model file has syntax errors
- `WLITE_SQLITE_ERROR`: a SQL execution failed during migration
- `WLITE_ERROR`: a generic error occurred

When `wlite_migrate` is used with error reporting, the error object includes the failing SQL statement and the SQLite error message. This makes debugging straightforward.

## Performance characteristics

The pipeline is designed to be fast for typical schemas. The parsing stage is O(n) in the size of the model file. The introspection stage is O(m) in the number of tables and columns. The diff stage is O(n + m). The planning stage is O(d) in the number of differences. The migration stage is limited by SQLite I/O.

For schemas with fewer than 100 tables, the entire planning phase (stages 1 through 4) takes microseconds. The migration phase is dominated by SQLite's own execution time, especially for table rebuilds that copy data.

## Summary

The data flow is linear and deterministic. A model file produces a schema. A database produces a schema. The diff identifies differences. The plan orders them. The migration executes them. Every stage is a pure function of its inputs. There is no hidden state, no side-channel configuration, and no ambiguous behavior.
