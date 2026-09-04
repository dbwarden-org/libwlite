---
title: SQLite Patterns
description: Table rebuilds, type normalization, collapse logic, and other SQLite-specific patterns in libwlite.
---

# SQLite Patterns

libwlite implements the same SQLite patterns as dbwarden. This page explains each pattern, why it exists, and when it triggers.

## Table rebuilds

### Why rebuilds exist

SQLite's `ALTER TABLE` is limited. It can rename a table, add a column, or rename a column (SQLite 3.25+). That is all. It cannot change a column type, change nullability, modify a default, drop a column (until 3.35), or alter constraints.

When you need to do any of these, you must rebuild the table: create a new table with the correct schema, copy the data, drop the old table, and rename the new one.

### When rebuilds trigger

A rebuild is triggered when any of these change:

| Change | Example |
|--------|---------|
| Column type | `TEXT` to `INTEGER` |
| Column nullability | `NULL` to `NOT NULL` |
| Column default | `DEFAULT 0` to `DEFAULT 1` |
| Column generated | Adding a generated column |
| Primary key | Adding or changing the PK |
| Unique constraint | Adding or removing UNIQUE |
| Check constraint | Adding or changing a CHECK |
| Foreign key | Adding or changing a REFERENCES |
| STRICT mode | Enabling or disabling STRICT |
| WITHOUT ROWID | Enabling or disabling WITHOUT ROWID |

### When rebuilds do NOT trigger

These changes use SQLite's `ALTER TABLE` directly:

| Change | Action |
|--------|--------|
| Add a column | `ALTER TABLE ... ADD COLUMN` |
| Drop a column | `ALTER TABLE ... DROP COLUMN` (SQLite 3.35+) |
| Rename a table | `ALTER TABLE ... RENAME TO` |
| Rename a column | `ALTER TABLE ... RENAME COLUMN` (SQLite 3.25+) |
| Add an index | `CREATE INDEX` |
| Drop an index | `DROP INDEX` |

### Rebuild SQL

A table rebuild generates this sequence:

```sql
-- 1. Create staging table with correct schema
CREATE TABLE users__wlite_new (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    email TEXT NOT NULL,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
);

-- 2. Copy data from old table
INSERT INTO users__wlite_new (id, name, email, created_at)
SELECT id, name, email, created_at FROM users;

-- 3. Drop old table (takes indexes with it)
DROP TABLE users;

-- 4. Rename staging to final name
ALTER TABLE users__wlite_new RENAME TO users;

-- 5. Recreate indexes
CREATE INDEX IF NOT EXISTS users_email ON users (email);
```

Note that the `CREATE TABLE` and `INSERT INTO ... SELECT` are separate statements. libwlite does not use SQLite's `CREATE TABLE ... AS SELECT` syntax because that syntax does not support constraints, defaults, or generated columns in the new table.

### Data preservation

During a rebuild, libwlite preserves all data. The `INSERT INTO ... SELECT` copies every row. If a new NOT NULL column is added without a DEFAULT, libwlite uses a reasonable fallback:

| Column type | Fallback value |
|-------------|----------------|
| `INTEGER` | `0` |
| `REAL` | `0.0` |
| `TEXT` | `''` (empty string) |
| `BLOB` | `X''` (empty blob) |
| `BOOLEAN` | `0` |
| `DATETIME` | `''` (empty string) |

These fallbacks are chosen to be safe and reversible. They do not violate any constraints. The empty string for TEXT is the most common fallback because TEXT columns are the most common.

### Staging table naming

The staging table name is derived from the original table name with a `__wlite_new` suffix. If the original table is `users`, the staging table is `users__wlite_new`. This suffix is short, unlikely to collide with user-defined tables, and clearly identifies the table as a temporary rebuild artifact.

### Column mapping during rebuild

When copying data from the old table to the staging table, libwlite maps columns by name. If the old table has columns `(id, name, email)` and the new schema has `(id, name, email, created_at)`, the INSERT statement is:

```sql
INSERT INTO users__wlite_new (id, name, email)
SELECT id, name, email FROM users;
```

The new column `created_at` gets its DEFAULT value. If a column is renamed, the mapping uses the old name in the SELECT and the new name in the INSERT.

## Collapse logic

### The problem

If you change two things about a table in the same migration (e.g., change a column type AND add a new column), a naive tool would rebuild the table twice. That is wasteful and slow.

### The solution

libwlite collapses multiple rebuilds on the same table into a single rebuild. The collapse happens during the planning stage, after the diff is computed.

### Example

```
-- Model changes:
--   1. field name text  -->  field name text { not_null }
--   2. add field bio text

-- Without collapse (2 rebuilds):
CREATE TABLE users__wlite_new (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    email TEXT NOT NULL
);
INSERT INTO users__wlite_new (id, name, email)
SELECT id, name, email FROM users;
DROP TABLE users;
ALTER TABLE users__wlite_new RENAME TO users;
CREATE TABLE users__wlite_new (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    email TEXT NOT NULL,
    bio TEXT
);
INSERT INTO users__wlite_new (id, name, email, bio)
SELECT id, name, email, NULL FROM users;
DROP TABLE users;
ALTER TABLE users__wlite_new RENAME TO users;

-- With collapse (1 rebuild):
CREATE TABLE users__wlite_new (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    email TEXT NOT NULL,
    bio TEXT
);
INSERT INTO users__wlite_new (id, name, email, bio)
SELECT id, name, email, NULL FROM users;
DROP TABLE users;
ALTER TABLE users__wlite_new RENAME TO users;
```

### When collapse does NOT apply

- Changes to different tables are never collapsed
- Adding a column (without other changes) does not trigger a rebuild, so there is nothing to collapse
- Drops and rebuilds on the same table are handled separately

### Collapse algorithm

The planner collects all changes for each table. It groups them into two categories: rebuild-triggering changes and non-rebuild changes. If there are any rebuild-triggering changes, all changes for that table are combined into a single rebuild sequence. Non-rebuild changes (like adding a column) are folded into the rebuild.

## Type normalization

### Why normalization exists

SQLite stores types as strings and uses affinity rules. The type `INT` and the type `INTEGER` are the same thing to SQLite, but a naive diff tool would see them as different and trigger an unnecessary migration.

libwlite normalizes types before comparison so equivalent types do not trigger migrations.

### Normalization rules

| Input type | Normalized to | Reason |
|------------|---------------|--------|
| `INT`, `INT2`, `INT4`, `INT8` | `INTEGER` | INTEGER affinity |
| `INTEGER`, `BIGINT`, `SMALLINT`, `TINYINT` | `INTEGER` | INTEGER affinity |
| `UNSIGNED INT`, `SIGNED`, `MEDIUMINT` | `INTEGER` | INTEGER affinity |
| `BOOLEAN`, `BOOL` | `INTEGER` | SQLite stores booleans as 0/1 |
| `REAL`, `DOUBLE`, `FLOAT` | `REAL` | REAL affinity |
| `NUMERIC`, `DECIMAL` | `REAL` | REAL affinity |
| `TEXT`, `VARCHAR`, `CHARACTER` | `TEXT` | TEXT affinity |
| `CLOB`, `NATIVE CHARACTER` | `TEXT` | TEXT affinity |
| `BLOB` | `BLOB` | No normalization |
| `DATE`, `DATETIME`, `TIMESTAMP` | `TEXT` | SQLite stores dates as text |
| `UUID`, `JSON` | `TEXT` | Custom types stored as text |
| Anything else | preserved as-is | Arbitrary type names |

### Example

```sql
-- Model says:  field age INTEGER
-- Database:    age INT

-- libwlite sees these as equivalent. No migration generated.
```

```sql
-- Model says:  field age INTEGER
-- Database:    age REAL

-- libwlite sees a type change. Migration generated:
-- CREATE TABLE users__wlite_new (...);
-- INSERT INTO users__wlite_new (...) SELECT ... FROM users;
-- DROP TABLE users;
-- ALTER TABLE users__wlite_new RENAME TO users;
```

### Case sensitivity

Type names are compared case-insensitively. `INTEGER`, `integer`, and `Integer` all normalize to `INTEGER`. This matches SQLite's own behavior.

### Affinity rules

SQLite determines column affinity from the type name using a set of rules. libwlite's normalization follows these rules. If the type name contains `INT`, the affinity is INTEGER. If it contains `CHAR`, `CLOB`, or `TEXT`, the affinity is TEXT. If it contains `BLOB` or no type, the affinity is BLOB. If it contains `REAL`, `FLOA`, or `DOUB`, the affinity is REAL. Otherwise, the affinity is NUMERIC.

## Default handling

### Semantic comparison

Default values are compared semantically, not textually. These pairs are equivalent:

| Value A | Value B | Reason |
|---------|---------|--------|
| `CURRENT_TIMESTAMP` | `current_timestamp` | Case-insensitive |
| `CURRENT_DATE` | `current_date` | Case-insensitive |
| `CURRENT_TIME` | `current_time` | Case-insensitive |
| `0` | `FALSE` | Boolean coercion |
| `1` | `TRUE` | Boolean coercion |
| `'text'` | `"text"` | String quoting |
| `NULL` | `null` | Case-insensitive |

### When defaults trigger rebuilds

Changing a default value does NOT trigger a rebuild by itself. However, if the default change is combined with other changes that require a rebuild (type change, nullability change, etc.), the new default is included in the rebuilt table definition.

### NULL defaults

A NULL default is treated as "no default." Removing a DEFAULT clause is equivalent to setting the default to NULL. Neither triggers a rebuild.

## Constraint diffing

### Primary keys

Primary keys are compared column-by-column. If the columns in the primary key change, a rebuild is triggered.

```sql
-- Model:  primary_key (id)
-- DB:     PRIMARY KEY (id, name)
-- Result: rebuild to change PK
```

### Unique constraints

Unique constraints are compared by their column sets. Adding or removing a unique constraint triggers a rebuild.

```sql
-- Model:  unique (user_id, email)
-- DB:     (no unique constraint)
-- Result: rebuild to add UNIQUE (user_id, email)
```

### Foreign keys

Foreign keys are compared by their target table, target column, and actions (ON DELETE, ON UPDATE). Any change triggers a rebuild.

```sql
-- Model:  references User.id on delete cascade
-- DB:     references User.id on delete restrict
-- Result: rebuild to change FK action
```

### Check constraints

Check constraints are compared by their expression. Any change triggers a rebuild.

```sql
-- Model:  check (end_time > start_time)
-- DB:     check (end_time >= start_time)
-- Result: rebuild to change CHECK expression
```

## STRICT mode

When a model has the `strict` option, libwlite generates:

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL
) STRICT;
```

STRICT mode enforces that every column must have a type and that values match the declared type. Enabling or disabling STRICT triggers a rebuild.

## WITHOUT ROWID

When a model has the `without rowid` option, libwlite generates:

```sql
CREATE TABLE user_preferences (
    user_id INTEGER,
    key TEXT,
    value TEXT,
    PRIMARY KEY (user_id, key)
) WITHOUT ROWID;
```

WITHOUT ROWID tables are smaller and faster for tables with a composite primary key and no integer primary key. Enabling or disabling WITHOUT ROWID triggers a rebuild.

## Partial indexes

Partial indexes include a `WHERE` clause that limits which rows are indexed:

```
index active_users {
    on users(email)
    where active = true
}
```

Generates:

```sql
CREATE INDEX active_users ON users (email) WHERE active = true;
```

Partial indexes are smaller and faster because they only index rows matching the WHERE clause.

## Generated columns

Generated columns are computed from an expression. They can be stored (computed on write) or virtual (computed on read).

```
field search_name text {
    generated (lower(name)) stored
}
```

Generates:

```sql
ALTER TABLE users ADD COLUMN search_name TEXT AS (lower(name)) STORED;
```

Changing a generated column's expression or type triggers a rebuild.

## What libwlite does NOT do

- **Data transformations**: libwlite only changes schema, not data. If you rename a column, the data is preserved. If you change a type, the data is copied as-is.
- **Backups**: libwlite does not back up your database before migration. Use your own backup strategy.
- **Incremental migrations**: each migration is the full diff from current state to desired state. There is no chain of revision scripts.
- **Migration history**: the schema state is the source of truth. libwlite does not track which migrations have been applied.
- **Data validation**: libwlite does not validate that existing data matches new constraints. If you add `NOT NULL` to a column with existing NULLs, the rebuild will use a fallback value.
