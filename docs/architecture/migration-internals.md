---
title: Migration Internals
description: How libwlite converts schema diffs into executable SQL, step by step.
---

# Migration Internals

This page explains how libwlite converts a schema diff into executable SQL. Understanding this helps you predict what libwlite will do and why.

## The problem

SQLite's `ALTER TABLE` is limited. It can:

- Rename a table
- Add a new column
- Rename a column (SQLite 3.25+)

It cannot:

- Change a column type
- Change a column's NULL/NOT NULL constraint
- Change a column's DEFAULT value
- Drop a column
- Add or modify table-level constraints (PRIMARY KEY, UNIQUE, CHECK, FOREIGN KEY)

When you need to do any of these, you must rebuild the table: create a new table with the correct schema, copy the data, drop the old table, and rename the new one.

## How libwlite handles this

### Step 1: Classify each difference

When libwlite compares the model against the database, each difference is classified:

| Classification | Example | Action |
|----------------|---------|--------|
| Additive | New column, new table | `ALTER TABLE ... ADD COLUMN` or `CREATE TABLE` |
| Subtractive | Drop column, drop table | `ALTER TABLE ... DROP COLUMN` or `DROP TABLE` |
| Alterable | Change default value | `ALTER COLUMN ... SET DEFAULT` (if supported) |
| Rebuild | Change column type | Full table rebuild |

The classification is done by the diff engine. The planner uses these classifications to decide which SQL to generate.

### Step 2: Expand rebuilds

A table rebuild is expanded into a sequence of SQL statements:

```sql
-- 1. Create staging table with correct schema
CREATE TABLE _staging_users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    email TEXT NOT NULL,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
);

-- 2. Copy data from old table
INSERT INTO _staging_users (id, name, email)
SELECT id, name, email FROM users;

-- 3. Drop old table
DROP TABLE users;

-- 4. Rename staging to final name
ALTER TABLE _staging_users RENAME TO users;
```

The staging table name is derived from the original table name with a `_staging_` prefix. This avoids collisions with other tables.

### Step 3: Plan ordering

The planner orders statements to respect dependencies:

1. Create new tables (no foreign key references yet)
2. Add columns to existing tables
3. Rebuild tables that need schema changes
4. Create indexes (after tables exist)
5. Create views and triggers

Foreign key dependencies affect ordering. If table B references table A, table A is created first. The planner performs a topological sort on the dependency graph.

### Step 4: Collapse rebuilds

Multiple rebuilds on the same table are collapsed into one. If you change both the type of a column and add a new column in the same migration, libwlite does one rebuild instead of two.

```
Before collapse:
  ALTER TABLE users ADD COLUMN created_at ...;
  ALTER TABLE users ALTER COLUMN name ...;  -- rebuild
  ALTER TABLE users ADD COLUMN bio ...;     -- another rebuild?

After collapse:
  ALTER TABLE users ADD COLUMN created_at ...;
  -- single rebuild with all changes
  CREATE TABLE _staging_users (...) AS SELECT ...;
  DROP TABLE users;
  ALTER TABLE _staging_users RENAME TO users;
```

The collapse happens during planning, after the diff is computed. The planner groups all rebuild-triggering changes for each table and produces a single rebuild sequence.

## Foreign key handling

When rebuilding a table with foreign keys, libwlite:

1. Disables foreign key checks during the rebuild (`PRAGMA foreign_keys = OFF`)
2. Creates the staging table with the correct foreign key constraints
3. Copies the data
4. Drops the old table
5. Renames the staging table
6. Re-enables foreign key checks

This prevents cascading deletes from firing during the rebuild.

### Cascading tables

If the rebuilt table is referenced by other tables with foreign keys, those references remain valid after the rebuild. SQLite tracks tables by name, and the rename at the end preserves the name. The foreign key references continue to point to the correct table.

However, if the rebuild changes the primary key columns, any foreign keys that reference those columns will break. libwlite does not automatically fix these. You must update the referencing tables in the same migration.

## Index handling

Indexes are rebuilt as part of the migration. If a table is rebuilt, its indexes are dropped and recreated. New indexes are created after the table they reference exists.

```sql
-- Table rebuild
CREATE TABLE _staging_users (...) AS SELECT ...;
DROP TABLE users;
ALTER TABLE _staging_users RENAME TO users;

-- Indexes recreated
CREATE INDEX IF NOT EXISTS users_email ON users (email);
CREATE INDEX IF NOT EXISTS users_username ON users (username);
```

Index creation uses `IF NOT EXISTS` to avoid errors if the index already exists. This is a safety measure, not a normal case.

## Comment handling

Table and column comments are stored using SQLite's `COMMENT ON` syntax (when supported) or as metadata in the schema. Changes to comments do not trigger table rebuilds.

## Rollback

Every migration is wrapped in a transaction. If any step fails, the entire migration is rolled back. The database returns to its pre-migration state.

```c
// libwlite_migrate wraps everything in:
BEGIN;
  CREATE TABLE _staging_users (...) AS SELECT ...;
  DROP TABLE users;
  ALTER TABLE _staging_users RENAME TO users;
  CREATE INDEX ...;
COMMIT;
-- If any step fails: ROLLBACK
```

The transaction ensures atomicity. Either all changes are applied or none are. There is no partial state.

## Checksums

After a successful migration, libwlite computes a checksum of the schema state. This checksum is used by `wlite_check` to verify the database matches the model without re-introspecting the entire schema.

The checksum is a SHA-256 hash of the normalized schema structure (table names, column names, types, constraints, defaults).

The checksum is computed from the introspected schema after migration, not from the model. This ensures the checksum reflects the actual database state.

## What libwlite does NOT do

- It does not generate data transformations (only schema changes)
- It does not back up data before migration (use your own backup strategy)
- It does not apply migrations incrementally (each migration is the full diff from current state to desired state)
- It does not track migration history (the schema state is the source of truth, not a chain of migrations)
- It does not validate data against new constraints (if you add NOT NULL to a column with existing NULLs, the rebuild uses a fallback value)

## Examples

### Adding a column

```sql
-- model adds: field bio text
ALTER TABLE users ADD COLUMN bio TEXT;
```

### Changing a column type

```sql
-- model changes: field age integer -> field age real
CREATE TABLE _staging_users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    age REAL
) AS SELECT id, name, age FROM users;
DROP TABLE users;
ALTER TABLE _staging_users RENAME TO users;
```

### Adding a NOT NULL column without DEFAULT

```sql
-- model adds: field email text { not_null }
-- SQLite cannot add NOT NULL without DEFAULT via ALTER TABLE
CREATE TABLE _staging_users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    email TEXT NOT NULL
) AS SELECT id, name, COALESCE(email, '') FROM users;
DROP TABLE users;
ALTER TABLE _staging_users RENAME TO users;
```

### Dropping a table

```sql
-- model removes: model OldTable { ... }
DROP TABLE old_table;
```

### Adding an index

```sql
-- model adds: index users_email { on users(email) }
CREATE INDEX IF NOT EXISTS users_email ON users (email);
```

### Changing a primary key

```sql
-- model changes: primary_key (id) -> primary_key (id, tenant_id)
CREATE TABLE _staging_users (
    id INTEGER,
    tenant_id INTEGER,
    name TEXT NOT NULL,
    PRIMARY KEY (id, tenant_id)
) AS SELECT id, tenant_id, name FROM users;
DROP TABLE users;
ALTER TABLE _staging_users RENAME TO users;
```

### Adding a foreign key

```sql
-- model adds: field org_id integer { references Organization.id }
-- This requires a rebuild because SQLite cannot add foreign keys via ALTER TABLE
CREATE TABLE _staging_users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    org_id INTEGER REFERENCES Organization(id)
) AS SELECT id, name, org_id FROM users;
DROP TABLE users;
ALTER TABLE _staging_users RENAME TO users;
```

### Enabling STRICT mode

```sql
-- model adds: strict
CREATE TABLE _staging_users (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL
) STRICT AS SELECT id, name FROM users;
DROP TABLE users;
ALTER TABLE _staging_users RENAME TO users;
```

## Edge cases

### Self-referencing foreign keys

If a table has a foreign key that references itself, the rebuild handles it correctly. The foreign key checks are disabled during the rebuild, so the self-reference does not cause a constraint violation.

### Circular foreign keys

If table A references table B and table B references table A, both tables must be rebuilt in a specific order. libwlite handles this by rebuilding tables in topological order, with foreign key checks disabled during the rebuild.

### Tables with no columns

SQLite allows tables with no columns. libwlite handles this edge case. The staging table has the same column list as the original (empty), and the data copy is a no-op.

### Views that reference rebuilt tables

Views are not affected by table rebuilds. Views are defined by SQL queries, not by table structure. If a view references a table that is rebuilt, the view continue to work after the rebuild because the table name is preserved.

### Triggers on rebuilt tables

Triggers that reference a rebuilt table are not affected. SQLite stores triggers by name and SQL text, not by table structure. The trigger continues to fire after the rebuild.

### Materialized views

libwlite does not support materialized views. SQLite does not have native materialized view support. If you need materialized views, implement them in your application layer.

### Tables with generated columns

Generated columns are handled correctly during rebuilds. The staging table includes the generated column definition, and SQLite computes the values during the INSERT. The data copy does not include the generated column in the SELECT list.

### Tables with STRICT mode

When rebuilding a STRICT table, the staging table includes the STRICT keyword. This ensures the rebuild preserves the strict type enforcement.

### Tables with WITHOUT ROWID

When rebuilding a WITHOUT ROWID table, the staging table includes the WITHOUT ROWID keyword. The primary key columns are included in the INSERT SELECT.

### Recovery from failed migration

If a migration fails, the transaction is rolled back and the database returns to its pre-migration state. There is no partial state. The error message includes the failing SQL and the SQLite error, making it straightforward to diagnose and fix the issue.

### Performance of rebuilds

Table rebuilds are the most expensive operation in a migration. The cost is proportional to the number of rows in the table. For large tables (millions of rows), a rebuild can take seconds to minutes. libwlite does not optimize this because SQLite's own INSERT ... SELECT is the fastest way to copy data.

### Multi-table migrations

When multiple tables need changes, libwlite processes them in dependency order. Tables with no foreign key dependencies are processed first. Tables that are referenced by other tables are processed before the tables that reference them. This ensures foreign key constraints are never violated during the migration.

## Summary

The migration pipeline is straightforward: classify, expand, order, collapse, execute. Each step is deterministic and produces predictable SQL. The transaction wrapper ensures atomicity. The checksum ensures verifiability. libwlite does not do anything surprising.
