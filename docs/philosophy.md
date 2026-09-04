---
title: Philosophy
description: The declarative philosophy behind wlite and how it compares to imperative migration tools.
---

# Philosophy

wlite follows the dbwarden principle: **your schema is the source of truth, not migration scripts**. You declare the tables, fields, and constraints you want in a `.wlite` model file. The tooling computes the diff against the live database and generates plain SQL to close the gap.

This document explains why that matters, how it compares to other approaches, and where each strategy fits best.

## Declarative vs imperative

Traditional migration tools take an imperative approach. You write a sequence of `ALTER TABLE`, `CREATE TABLE`, and `DROP COLUMN` statements by hand. Each migration is a snapshot of a transformation, and the tool applies them in order. This has several costs:

- You must reason about the current state of the database to write the next migration correctly.
- Migrations interact. A column rename in migration 3 might conflict with a column add in migration 5 if you are not careful.
- Rollback logic is separate code that must be maintained in parallel.
- CI must test every migration path, not just the final desired state.

The declarative approach is different. You describe the end state:

```
model User {
    table "users"

    field id integer {
        primary_key
        autoincrement
    }

    field username text {
        not_null
        unique
    }

    field email text {
        not_null
    }

    field created_at datetime {
        not_null
        default CURRENT_TIMESTAMP
    }
}
```

Then wlite compares that model against the live database and generates the minimal SQL to reach the desired state. If the table does not exist, it creates it. If columns are missing, it adds them. If types have changed, it rebuilds the table. You never write `ALTER TABLE` by hand.

This means:

- The model file is always self-contained. You can read it to understand the complete schema.
- There are no ordering concerns. The diff algorithm computes what needs to change regardless of how you got to the current state.
- Rollback is implicit. To go back, you revert the model file and run migrate again.
- CI tests the desired state, not a chain of historical transformations.

## Side by side: imperative vs declarative

Consider a team that needs to add a `phone_number` column to the `users` table. Here is what each approach looks like in practice.

### Imperative (Alembic-style)

You generate a new migration file, write the SQL, and commit it:

```
# alembic/versions/0007_add_phone_number.py

def upgrade():
    op.add_column('users', sa.Column('phone_number', sa.String(20), nullable=True))

def downgrade():
    op.drop_column('users', 'phone_number')
```

You must remember to run the migration. You must remember the correct column type. You must decide whether the column is nullable. If someone else adds a conflicting migration between yours and the next deploy, you have a merge conflict to resolve.

### Declarative (wlite)

You add the field to the model file:

```
model User {
    table "users"

    field id integer {
        primary_key
        autoincrement
    }

    field username text {
        not_null
        unique
    }

    field email text {
        not_null
    }

    field phone_number text {
        nullable
    }

    field created_at datetime {
        not_null
        default CURRENT_TIMESTAMP
    }
}
```

You run `wlite migrate`. wlite detects the new column and generates:

```sql
ALTER TABLE "users" ADD COLUMN "phone_number" TEXT;
```

There is no migration file to write, no downgrade function to maintain, and no ordering to think about. The model file remains the single source of truth.

### What happens next week

Next week, another developer adds a `bio` column to the same table. With the imperative approach, both developers wrote migrations and one of them must resolve the ordering. With the declarative approach, both developers add fields to the model file, and wlite computes the minimal diff regardless of who committed first.

## Side by side: renaming a column

Column renames are where imperative tools get complicated.

### Imperative

```
def upgrade():
    op.alter_column('users', 'name', new_column_name='full_name')
```

The imperative tool has to decide whether this is a rename or a drop-plus-add. Some tools track this with special flags. Some tools just do a drop-plus-add and lose data. The developer must know which behavior their tool uses and plan accordingly.

### Declarative

You change the field name in the model:

```
field full_name text {
    not_null
}
```

wlite detects that `full_name` is missing and `name` exists. It generates a rename migration using `ALTER TABLE "users" RENAME COLUMN "name" TO "full_name"`. Data is preserved. The developer does not need to think about the mechanics.

## Side by side: dropping a column

### Imperative

```
def upgrade():
    op.drop_column('users', 'phone_number')
```

You must remember to remove the column from every migration that references it. If migration 5 added the column and migration 12 drops it, you need to make sure that running migrations 1 through 11 and then rolling back to 5 still works correctly. In practice, most teams do not test rollback paths thoroughly.

### Declarative

You remove the field from the model. wlite detects the column exists in the database but not in the model, and generates:

```sql
ALTER TABLE "users" DROP COLUMN "phone_number";
```

No rollback script to maintain. If you want the column back, you add it to the model again.

## The same quality as dbwarden

The SQLite3 backend in dbwarden handles table rebuilds, collapse logic, type normalization, default handling, and constraint diffing. This is the reference implementation. libwlite mirrors it exactly. A CI workflow enforces behavioral sync between the two projects.

When dbwarden improves how it handles a type, default, or constraint, those improvements flow into libwlite. When libwlite finds an edge case in SQLite schema diffing, the fix propagates back to dbwarden. The two projects share a common core of schema management logic, expressed in different languages for different audiences.

This means you get the same algorithm whether you are using dbwarden from Ruby or wlite from C, C++, or another language with bindings. The diff output is identical. The migration SQL is identical. The behavior is identical.

## Compared to raw SQLite

Raw SQLite gives you full control but no schema management. You write DDL statements by hand and track them in files or scripts. There is no diffing, no validation, and no guarantee that your DDL matches what you think the schema looks like.

Consider a developer who writes this in a setup script:

```sql
CREATE TABLE IF NOT EXISTS users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    email TEXT NOT NULL UNIQUE,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);
```

Three months later, someone adds a column with a separate script:

```sql
ALTER TABLE users ADD COLUMN phone_number TEXT;
```

Now the setup script says the table has four columns, but the database has five. New developers who run the setup script from scratch get a different schema than developers who ran the incremental script. This is schema drift, and it is invisible until something breaks.

wlite adds declarative schema management on top of SQLite without changing the database engine itself. The model file always describes the complete schema. The diff algorithm always computes what needs to change. There is no drift.

## Compared to SQLAlchemy + Alembic

Alembic is the standard migration tool for SQLAlchemy. It works well, but it has real costs:

- **Python dependency**: Alembic requires Python, SQLAlchemy, and a working Alembic configuration. If your application is not in Python, this is a significant overhead.
- **Migration chain**: Alembic maintains a chain of numbered migrations. Each migration references its parent. If you delete or reorder a migration, the chain breaks.
- **Autogenerate limitations**: Alembic can autogenerate migrations by comparing SQLAlchemy models to the database, but it often misses constraints, defaults, and column types that are not expressed in the model. You end up editing generated files by hand.
- **Downgrade scripts**: Alembic requires you to write downgrade functions. Most teams do not test these. They rot.
- **Branching conflicts**: When two developers add migrations on different branches, you get a merge conflict in the migration chain. Resolving it requires careful ordering and sometimes manual edits.

wlite does not require SQLAlchemy, does not require Python (if you use the C or C++ bindings), and does not maintain a migration chain. You describe the desired state and wlite computes the diff. There are no numbered migrations to order, no downgrade functions to maintain, and no merge conflicts in a chain.

### When Alembic is the right choice

If your application is in Python, uses SQLAlchemy extensively, and needs runtime model introspection, Alembic may be the better tool. Alembic integrates tightly with SQLAlchemy's ORM, and if you are already paying the cost of that dependency, the marginal cost of Alembic is low.

wlite is designed for teams that want schema management without the ORM tax. If your application is not in Python, or if you want a tool that works across languages, wlite is the better choice.

## Compared to other SQLite migration tools

Many SQLite migration tools are thin wrappers around hand-written SQL. They track which migrations have been applied and skip ones that have already run. They do not diff your desired schema against the live schema.

Common tools in this category include:

- **yoyo-migrations**: A Python tool that tracks applied migrations in a table. You write SQL by hand.
- **sqlite-migrations**: A PHP tool that tracks applied migrations. You write SQL by hand.
- **dbmate**: A tool that tracks applied migrations. You write SQL by hand.

All of these tools solve the same problem: apply SQL scripts in order and remember which ones have run. None of them solve the problem of computing what SQL to write in the first place.

wlite performs a full schema diff, including type normalization, default handling, and constraint analysis. It produces minimal SQL regardless of the current state. The other tools apply whatever SQL you wrote, even if it is redundant or incorrect.

## Compared to no schema management at all

The most common approach to SQLite schema management is no approach at all. Developers write `CREATE TABLE IF NOT EXISTS` and hope for the best. Columns are added with `ALTER TABLE ... ADD COLUMN` and never validated against a reference. This works until it does not, and then debugging is painful.

Common failure modes of no schema management:

- **Drift between environments**: Development, staging, and production databases have different schemas because incremental changes were applied unevenly.
- **Onboarding confusion**: New developers cannot figure out what the current schema looks like because there is no reference. They have to inspect the running database or read migration scripts in order.
- **Silent data loss**: A column is dropped in one environment but not another. Queries that reference the missing column fail silently or return wrong results.
- **CI gaps**: Tests pass because they run against a schema that is different from production. Bugs appear only in production.

wlite gives you a single source of truth that can be validated, versioned, and tested. The model file is always the complete schema. The diff algorithm always produces the correct SQL. CI tests the desired state.

## Cost analysis

### Developer time

Imperative migrations require the developer to:

1. Understand the current state of the database.
2. Decide what changes are needed.
3. Write the migration SQL.
4. Write the rollback SQL.
5. Test the migration forward.
6. Test the rollback backward (rarely done).
7. Resolve merge conflicts if another developer added a migration in the same area.

wlite requires the developer to:

1. Update the model file.
2. Run `wlite migrate`.
3. Review the generated SQL.

The declarative approach eliminates steps 1, 4, 6, and 7 from the developer's workflow. The time savings compound across a team. A team of five developers making ten schema changes per week saves roughly five hours per week on migration bookkeeping alone.

### CI time

Imperative CI must:

1. Start with a clean database.
2. Apply every migration in order, one by one.
3. Run the test suite against the resulting schema.
4. Optionally, test each migration individually.

wlite CI must:

1. Start with a clean database.
2. Run `wlite migrate`.
3. Run the test suite against the resulting schema.

There is no migration chain to replay. CI runs faster and is simpler to maintain. For projects with hundreds of migrations, the difference can be significant. A project with 200 migrations that each take 0.1 seconds to apply saves 20 seconds per CI run. Over thousands of CI runs per month, this adds up.

### Maintenance burden

Imperative migrations accumulate. After two years, a project might have 300 migration files. Each one must be understood, maintained, and occasionally debugged. Deleting a migration breaks the chain. Reordering a migration can change the meaning of subsequent migrations.

wlite has no migration chain. The model file is the only artifact that must be maintained. Old migration files are not needed because wlite always computes the diff from the current state. The maintenance burden is constant regardless of how many changes have been made.

### Summary table

| Cost category | Imperative | Declarative (wlite) |
|---------------|------------|---------------------|
| Developer time per change | 20-40 minutes | 5-10 minutes |
| CI complexity | High (replay chain) | Low (single migrate) |
| Merge conflict risk | High (migration ordering) | None (model file merge) |
| Maintenance burden | Grows with project age | Constant |
| Rollback cost | Separate script, rarely tested | Revert model, re-migrate |
| Onboarding time | Read entire migration history | Read model file |

## Migration chain problems

Migration chains are the fundamental weakness of imperative tools. Here are concrete problems that arise in real projects.

### The merge conflict

Two developers add migrations on different branches:

- Branch A adds migration 0047: `ALTER TABLE users ADD COLUMN bio TEXT;`
- Branch B adds migration 0047: `ALTER TABLE users ADD COLUMN avatar_url TEXT;`

Both migrations claim to be number 0047. When the branches merge, one developer must rename their migration to 0048. But now the chain order depends on which migration runs first, and the developer must reason about whether the order matters.

With wlite, both developers add fields to the model file. The merge conflict is in the model file, which is a simple text merge. wlite computes the correct diff regardless of which field was added first.

### The deleted migration

A developer deletes migration 0023 because it was superseded by migration 0045. The chain breaks. Every migration after 0023 references 0022 as its parent, but migration 0024 expects 0023 to exist.

With wlite, there are no numbered migrations. The model file is the only artifact. There is nothing to delete and nothing to break.

### The reordered migration

A developer realizes that migration 0015 must run before migration 0010 for the schema to be consistent. They reorder the migrations. Now every migration after 0015 has the wrong parent reference. The entire chain must be renumbered.

With wlite, ordering is irrelevant. The diff algorithm computes the correct SQL regardless of the order in which changes were made.

### The long chain

After three years, a project has 400 migrations. Running `alembic upgrade head` from scratch takes five minutes because each migration is applied individually. New developers must wait five minutes for the database to be ready. CI must wait five minutes for the database to be ready.

With wlite, `wlite migrate` computes the diff once and applies the minimal SQL. The time to reach the desired state is the time to execute the resulting SQL, not the time to replay 400 historical transformations.

### The failed migration

Migration 0150 fails halfway through. The database is now in an unknown state: some changes from 0150 were applied, others were not. The developer must figure out which changes were applied and manually fix the database before retrying.

With wlite, the diff is computed from the current state. If a previous migration failed, the diff will include the changes that were not applied. There is no partial state to reason about.

## How wlite's diff algorithm works at a high level

wlite's diff algorithm compares the model file against the live database schema and computes the minimal set of SQL statements needed to transform the database into the desired state.

### Step 1: introspect the database

wlite reads the live database schema using SQLite's `sqlite_master` table and `PRAGMA table_info`. It collects:

- Table names and their definitions.
- Column names, types, nullability, defaults, and primary key status.
- Indexes and their columns.
- Constraints and their definitions.

### Step 2: parse the model file

wlite parses the `.wlite` model file and extracts the desired schema:

- Model names and their table mappings.
- Field names, types, constraints, and defaults.
- Indexes and constraints declared in the model.

### Step 3: normalize

SQLite has flexible type affinity. A column defined as `VARCHAR(255)` and a column defined as `TEXT` are equivalent in SQLite. wlite normalizes types to a canonical form before comparison. This prevents false positives where the model says `TEXT` but the database says `VARCHAR`.

### Step 4: compute the diff

wlite compares the normalized model schema against the normalized database schema:

- Tables in the model but not in the database need to be created.
- Tables in the database but not in the model need to be dropped (or flagged for review).
- Columns in the model but not in the database need to be added.
- Columns in the database but not in the model need to be removed.
- Columns that exist in both but differ in type, nullability, or default need to be rebuilt.

For table rebuilds, wlite uses SQLite's `CREATE TABLE ... AS SELECT` pattern to preserve data while restructuring the table.

### Step 5: generate SQL

wlite generates the minimal SQL to close the gap. It produces standard SQL that can be reviewed, modified, and run outside of wlite if needed. The generated SQL is deterministic: the same model and database state always produce the same SQL.

### What makes this different

Other tools generate SQL by comparing migration files against the database. wlite generates SQL by comparing the desired state against the actual state. This is a fundamental difference. The migration-based approach requires you to have written the correct migration. The state-based approach only requires you to have described the correct end state.

## Why plain SQL output matters

wlite generates plain SQL, not a proprietary format. This matters for several reasons.

### Auditability

You can read the generated SQL and understand exactly what wlite will do. There is no magic. If wlite generates `ALTER TABLE users ADD COLUMN phone_number TEXT`, you can verify that this is the correct change before running it.

### Portability

The generated SQL works with any SQLite tool. You can run it with the SQLite CLI, with a Python script, or with wlite. You are not locked into wlite's runtime.

### Debugging

If something goes wrong, you can copy the generated SQL and run it manually. You can inspect each statement. You can run them one at a time to isolate the problem. With opaque migration tools, debugging often requires stepping through the tool's internals.

### Integration

The generated SQL can be incorporated into other workflows. You might run `wlite migrate --dry-run` to generate the SQL, review it, and then apply it as part of a larger deployment script. This gives you control over when and how changes are applied.

## The role of the model file as documentation

The `.wlite` model file is not just an input to the tool. It is documentation.

### Complete schema description

The model file describes the entire schema in one place. A new developer can read it and understand every table, every column, and every constraint. There is no need to read migration files, inspect the running database, or guess at what columns exist.

### Intent, not history

Migration files describe what changed. The model file describes what exists. The model file says "users have a phone number." A migration file says "add a phone number column." The model file communicates intent. The migration file communicates history. Intent is more useful for understanding the schema.

### Version control friendly

The model file is a single file that changes when the schema changes. It produces clean diffs in version control. You can see exactly what changed in a pull request. Migration files produce noisy diffs because they include boilerplate, comments, and ordering information.

### Searchable

You can search the model file for a table name, column name, or constraint. You can grep for `phone_number` and find every model that uses it. With migration files, you must search through hundreds of files to find where a column was added, modified, or dropped.

### Self-contained

The model file does not depend on other files. It does not reference migration numbers or parent commits. It is a complete description of the desired state. This makes it easy to share, review, and maintain.

## What declarative gives you

| Aspect | Imperative (Alembic, Flyway, etc.) | Declarative (wlite, dbwarden) |
|--------|-------------------------------------|-------------------------------|
| Source of truth | Migration chain | Model file |
| Current state | Implicit (sum of all migrations) | Explicit (the model) |
| Rollback | Hand-written downgrade scripts | Revert model, re-migrate |
| CI testing | Test every migration path | Test desired state only |
| Ordering | Critical (migrations must be ordered) | Irrelevant (diff computes changes) |
| Drift detection | Manual | Automatic |
| New developer onboarding | Read entire migration history | Read model file |
| Merge conflict risk | High (migration numbering) | Low (model file merge) |
| Maintenance burden | Grows with project age | Constant |
| Data preservation on column rename | Varies by tool | Automatic (RENAME COLUMN) |
| Cross-language support | Usually one language | Any language with bindings |
| Generated SQL review | Sometimes possible | Always possible |
| Schema documentation | Spread across migration files | Single model file |
| Time to apply from scratch | Proportional to number of migrations | Proportional to schema complexity |
| Failed migration recovery | Manual state inspection | Re-run migrate (diff from current state) |

## What declarative does NOT give you

- **Data transformations**: wlite only changes schema, not data. If you rename a column, the data is preserved as-is. If you need to transform data, do it separately. For example, if you need to split a `name` column into `first_name` and `last_name`, wlite can add the new columns but the data migration must be handled in application code or a separate SQL script.
- **Backups**: wlite does not back up your database before migration. Use your own backup strategy. Always test your backup and restore process separately from schema management.
- **Migration history**: the schema state is the source of truth. wlite does not track which migrations have been applied. If you need an audit trail of schema changes, use version control on the model file.
- **Incremental migrations**: each migration is the full diff from current state to desired state. There is no chain of revision scripts. This is a feature, not a limitation, but it means you cannot replay a single historical migration.
- **Cross-database support**: wlite is designed for SQLite. It does not support PostgreSQL, MySQL, or other databases. If you need cross-database schema management, use a tool like Alembic or Flyway.
- **Runtime schema inspection**: wlite is a build-time tool. It does not inspect or modify schemas at runtime. If your application needs to modify its schema at runtime, you need a different approach.
- **Automatic data backup before rebuild**: when wlite rebuilds a table (for type or constraint changes), it uses `CREATE TABLE ... AS SELECT` to preserve data. This is not a full backup. If the rebuild fails partway through, you may lose data. Always back up before running migrations on production databases.

## Real world scenarios

### Scenario 1: startup with fast iteration

A three-person startup is building an application with SQLite. They make schema changes daily. The imperative approach would require them to write and maintain dozens of migration files per week. With wlite, they update the model file and run migrate. The time savings let them iterate faster.

### Scenario 2: enterprise with compliance requirements

A large company needs an audit trail of every schema change. They use version control on the model file and review every change in a pull request. The model file diff shows exactly what changed. They do not need migration files because the version control history is the audit trail.

### Scenario 3: open source library

An open source library uses SQLite for local storage. Contributors use many different languages. The library provides wlite model files and uses libwlite bindings in each language. The schema is described once and managed consistently across all language implementations.

### Scenario 4: legacy migration

A team has a legacy database with no schema management. They need to bring it under control without losing data. They create a model file that describes the current schema, run `wlite migrate --dry-run` to see the diff, and gradually adopt the declarative approach. They do not need to rewrite their entire history.

### Scenario 5: multi-branch development

Five developers are working on different features that all touch the database schema. With imperative tools, their migrations conflict regularly. With wlite, they all edit the model file. Merge conflicts are simple text conflicts in a single file, not ordering problems in a migration chain.

## Summary

wlite's declarative approach is not just a different way to write the same thing. It is a fundamentally different way to think about schema management. Instead of tracking how the schema changed, you describe what the schema should be. Instead of replaying history, you compute the diff. Instead of maintaining hundreds of migration files, you maintain one model file.

The model file is the source of truth. The diff algorithm computes the path from the current state to the desired state. The generated SQL is plain, reviewable, and portable. The result is a simpler, more reliable, and more maintainable approach to SQLite schema management.
