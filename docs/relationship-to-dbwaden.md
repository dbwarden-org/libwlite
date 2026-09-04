---
title: Relationship to dbwarden
description: How wlite relates to dbwarden, shared core logic, CI synchronization, and when to use which.
---

# Relationship to dbwarden

dbwarden is a declarative schema compiler for Python and SQLAlchemy. It is
feature-rich: multi-database support, plugin systems, async drivers, seed
management, and more.

wlite is what happens when you take dbwarden's SQLite3 engine and remove
everything except SQLite. The table rebuild algorithms, type normalization,
collapse logic, and constraint diffing that make dbwarden's SQLite support
production-grade are implemented in libwlite as a standalone C library.

This document explains how the two projects relate, how they diverge, how their
shared logic stays synchronized, and when you should choose one over the other.

## History and motivation

dbwarden began as a Python-based tool for managing PostgreSQL schemas through
SQLAlchemy models. Over time, SQLite support was added as a convenience layer.
The SQLite backend grew more sophisticated with each release. Table rebuilds
became safer, type mapping became more complete, and collapse logic reduced the
number of destructive operations on large tables.

By the time dbwarden reached version 3.x, the SQLite backend was a fully
separate engine. It handled type normalization, constraint diffing, index
rebuilds, and table rewrites with the same rigor as the PostgreSQL engine. But
it was still bundled inside a Python package that required SQLAlchemy, pip, and
a runtime interpreter.

wlite was created to extract that SQLite engine into something that could run
without Python. The goal was a single C library that any language could link
against. The project started by isolating the core schema diffing algorithm from
dbwarden's SQLite backend and rewriting it in portable C.

The extraction followed these steps:

1. Identify all SQLite-specific operations in dbwarden's backend
2. Define a clean C API for each operation
3. Implement the C versions with identical behavior
4. Verify equivalence through a shared test harness
5. Publish libwlite as a standalone library
6. Build the wlite CLI on top of libwlite
7. Create language bindings for Rust, Go, Python, Zig, and others

This process took several months and required careful attention to edge cases
that existed in the Python implementation but were never documented explicitly.

## Shared core logic

The schema diffing engine is the same in both projects. When dbwarden handles
a `TEXT` column with a `DEFAULT ''` constraint, libwlite handles it
identically. When dbwarden rebuilds a table to change a column type, libwlite
performs the same rebuild with the same safety guarantees. The implementation
language is different but the algorithm is the same.

The shared behaviors include:

### Table rebuilds

When SQLite's `ALTER TABLE` cannot express a change, both tools rebuild the
table by creating a new one, copying data, dropping the old, and renaming.

The rebuild sequence for both dbwarden and libwlite is:

1. `BEGIN IMMEDIATE` transaction
2. `CREATE TABLE _new_<name> (...schema...)`
3. `INSERT INTO _new_<name> SELECT ... FROM <name>`
4. `DROP TABLE <name>`
5. `ALTER TABLE _new_<name> RENAME TO <name>`
6. Recreate indexes on the renamed table
7. `COMMIT`

Both implementations use `BEGIN IMMEDIATE` rather than `BEGIN DEFERRED` to
prevent writers from starving under concurrent reads. Both implementations
verify that the row count before and after the rebuild matches. Both
implementations retry with an exponential backoff if the rebuild fails due to
a locked database.

### Collapse logic

Multiple rebuilds on the same table are collapsed into a single rebuild.
If a schema change requires altering three columns on the same table, both
tools recognize that these operations can be combined into one rebuild pass
rather than three separate rebuilds.

The collapse algorithm works by collecting all pending operations on a table
and sorting them by dependency. Operations that do not depend on each other
can be merged. The merged operation creates the new table with all changes
applied at once, then copies data with the appropriate column mapping.

This reduces the total number of table rebuilds from O(n) to O(1) per table,
where n is the number of column changes.

### Type normalization

SQLite uses dynamic typing. The following type names are equivalent in both
dbwarden and libwlite:

| Input type | Normalized to |
|------------|---------------|
| `INT` | `INTEGER` |
| `INTEGER` | `INTEGER` |
| `TINYINT` | `INTEGER` |
| `SMALLINT` | `INTEGER` |
| `MEDIUMINT` | `INTEGER` |
| `BIGINT` | `INTEGER` |
| `UNSIGNED BIG INT` | `INTEGER` |
| `INT8` | `INTEGER` |
| `BOOLEAN` | `INTEGER` |
| `BOOL` | `INTEGER` |
| `TEXT` | `TEXT` |
| `CLOB` | `TEXT` |
| `VARCHAR` | `TEXT` |
| `NVARCHAR` | `TEXT` |
| `VARYING CHARACTER` | `TEXT` |
| `CHARACTER` | `TEXT` |
| `REAL` | `REAL` |
| `DOUBLE` | `REAL` |
| `DOUBLE PRECISION` | `REAL` |
| `FLOAT` | `REAL` |
| `NUMERIC` | `REAL` |
| `DATETIME` | `TEXT` |
| `TIMESTAMP` | `TEXT` |
| `DATE` | `TEXT` |
| `TIME` | `TEXT` |

When a column type appears in a schema definition, both tools normalize it
before comparison. This prevents false positives in the diff engine. A schema
that declares `BOOL` will match an existing `INTEGER` column without
triggering a rebuild.

### Default handling

Default values undergo similar normalization. The following pairs are treated
as identical:

- `CURRENT_TIMESTAMP` and `current_timestamp`
- `CURRENT_DATE` and `current_date`
- `CURRENT_TIME` and `current_time`
- `'true'` and `1` (for boolean columns)
- `'false'` and `0` (for boolean columns)
- `NULL` and no default (when the column is nullable)
- `''` and no default (for text columns, depending on context)

Both tools apply this normalization when comparing the desired schema to the
existing schema. A column with `DEFAULT current_timestamp` will not be
recreated just because the desired schema says `DEFAULT CURRENT_TIMESTAMP`.

### Constraint diffing

Primary keys, unique constraints, foreign keys, and check constraints are
compared individually. The diff engine examines each constraint type
separately and produces a minimal set of changes.

For primary keys, both tools recognize that SQLite only supports implicit rowid
primary keys. A declared `PRIMARY KEY` on a non-integer column triggers a
table rebuild in both implementations.

For foreign keys, both tools compare the referenced table, referenced column,
on delete action, and on update action. A change in any of these fields
triggers a rebuild.

For unique constraints, both tools compare the set of columns in each unique
constraint. A change in column order does not trigger a rebuild because SQLite
treats `(a, b)` and `(b, a)` as the same unique constraint for indexing
purposes but not for enforcement purposes. Both tools preserve the declared
order.

For check constraints, both tools compare the SQL expression string
character by character. Whitespace differences are normalized. Parenthetical
grouping differences are normalized. Otherwise, the expression must match
exactly.

### Index management

Both tools manage indexes as part of the rebuild process. When a table is
rebuilt, all non-automatic indexes are dropped and recreated. The rebuild
process preserves index order, index uniqueness, and index column lists.

Both tools also handle partial indexes (indexes with a `WHERE` clause) and
expression-based indexes. These index types are compared by their full SQL
definition.

## The shared .wlite format

dbwarden and wlite share the `.wlite` model file format. This is a
declarative format for describing SQLite schemas. dbwarden can read `.wlite`
files directly, allowing a single schema definition to drive both the
dbwarden and wlite workflows.

A `.wlite` file describes tables, columns, constraints, and indexes in a
plain text format that is easy to read and version control.

A minimal `.wlite` file looks like this:

```
TABLE users (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    email TEXT UNIQUE,
    created_at TEXT DEFAULT CURRENT_TIMESTAMP
)

TABLE posts (
    id INTEGER PRIMARY KEY,
    author_id INTEGER NOT NULL REFERENCES users(id),
    title TEXT NOT NULL,
    body TEXT,
    published_at TEXT
)

INDEX idx_posts_author ON posts(author_id)
```

The format supports:

- `TABLE` declarations with column definitions
- Column types using SQLite type names
- `NOT NULL` constraints
- `DEFAULT` values (integers, strings, expressions)
- `PRIMARY KEY` (single column or composite)
- `UNIQUE` constraints
- `FOREIGN KEY ... REFERENCES` declarations
- `CHECK` constraints with SQL expressions
- `INDEX` declarations (unique and non-unique)
- Partial indexes with `WHERE` clauses
- Composite indexes on multiple columns
- Comments using `#` or `--` syntax
- Blank lines for visual separation

dbwarden reads `.wlite` files through its SQLite backend. It parses the
format, builds an internal representation, and uses the same diff engine as
wlite. This means a single `.wlite` file can drive schema management in both
tools without modification.

## CI synchronization

A CI workflow keeps libwlite's behavior synchronized with dbwarden's SQLite
backend. The workflow runs identical test suites against both implementations
and fails if the outputs diverge. This means improvements to dbwarden's schema
management automatically benefit wlite, and fixes in wlite propagate back to
dbwarden.

### The sync workflow

The synchronization runs on every push to either repository's main branch.
It also runs on pull requests that touch schema-related code.

The workflow performs these steps:

1. Checks out both repos side by side
2. Verifies that key rebuild operations exist in both implementations
3. Verifies that type normalization patterns match
4. Builds and runs libwlite's test suite
5. Builds the wlite CLI and runs a smoke test
6. Compares output SQL from both tools for a set of reference schemas
7. Reports any divergence

### Step-by-step detail

**Checkout**: The CI runner clones both the dbwarden and wlite repositories
into a shared workspace. The repos are placed in sibling directories so that
file paths can be resolved between them.

**Operation verification**: The CI script greps both codebases for the
essential SQLite operations. It confirms that both implement `CREATE TABLE`,
`INSERT INTO`, `DROP TABLE`, `ALTER TABLE RENAME TO`, `CREATE INDEX`, and
`BEGIN IMMEDIATE`. If either codebase is missing an operation, the workflow
fails immediately.

**Type normalization verification**: The CI script compares the type
normalization tables in both implementations. It extracts the mapping from
Python source in dbwarden and from C source in libwlite, then checks that
every type in one appears in the other. This prevents drift where one tool
normalizes a type that the other does not.

**Test suite execution**: libwlite's test suite is compiled and run. The test
suite covers table rebuilds, collapse logic, type normalization, default
handling, constraint diffing, and index management. Each test case defines an
input schema, a desired schema, and the expected SQL output. The test runner
verifies that the SQL output matches the expected result.

**CLI smoke test**: The wlite binary is built from source and run against a
set of reference `.wlite` files. The smoke test verifies that the CLI can
parse `.wlite` files, generate SQL, and exit cleanly.

**SQL comparison**: The most important step. The CI script feeds a set of
reference schemas to both dbwarden and wlite and captures the generated SQL.
It then performs a line-by-line comparison of the SQL output. Any difference
in the generated SQL causes the workflow to fail.

**Reporting**: If all checks pass, the workflow reports success. If any check
fails, the workflow logs the specific failure and the line-level differences
in SQL output.

### What triggers a sync failure

A sync failure can be caused by:

- Adding a new type normalization in one tool but not the other
- Changing the rebuild sequence in one tool but not the other
- Fixing a bug in one tool's diff engine but not propagating it to the other
- Changing the SQL output format (whitespace, keyword casing, ordering)
- Adding a new constraint type in one tool but not the other

When a sync failure occurs, the developer must update the other repository to
match. This is typically a straightforward change because the two
implementations share the same algorithm even though they use different
languages.

### Bidirectional flow

Improvements flow in both directions. When dbwarden adds support for a new
constraint type, libwlite adds the same support. When libwlite finds an edge
case in SQLite schema diffing, the fix propagates back to dbwarden. The CI
workflow ensures that neither project falls behind.

This bidirectional flow means that both projects benefit from each other's
user base. dbwarden's large Python community exercises the SQLite backend in
ways that wlite's smaller community might not. wlite's use in embedded
systems and firmware projects surfaces edge cases that dbwarden's Python
users might not encounter.

## Feature comparison

| Feature | dbwarden | wlite |
|---------|----------|-------|
| SQLite schema management | Yes | Yes |
| PostgreSQL support | Yes | No |
| MySQL support | Yes | No |
| MariaDB support | Yes | No |
| ClickHouse support | Yes | No |
| SQLAlchemy integration | Yes | No |
| Python models | Yes | No |
| .wlite model files | Yes | Yes |
| Plain SQL output | Yes | Yes |
| Table rebuilds | Yes | Yes |
| Collapse logic | Yes | Yes |
| Type normalization | Yes | Yes |
| Constraint diffing | Yes | Yes |
| Index management | Yes | Yes |
| Plugin system | Yes | No |
| Async operation | Yes | No |
| Seed management | Yes | No |
| Schema snapshots | Yes | No |
| Impact analysis | Yes | No |
| Live database reverse-engineering | Yes | No |
| CLI tool | Yes | Yes |
| C library (libwlite) | No | Yes |
| Language bindings | No | Yes (Rust, Go, Zig, C#, Python) |
| Single binary distribution | No | Yes |
| Embedded/firmware support | No | Yes |
| TUI interface | No | Yes (wlite-tui) |
| Zero runtime dependencies | No | Yes (only SQLite3) |

## Use case comparison

| Use case | Recommended tool | Reason |
|----------|-----------------|--------|
| Web application with PostgreSQL | dbwarden | Native PostgreSQL support |
| Web application with SQLite | Either | Both handle SQLite equally |
| CLI tool for schema management | wlite | Single binary, no runtime |
| Embedded firmware database | wlite | C library, no Python needed |
| Microservice with SQLAlchemy | dbwarden | Natural Python integration |
| IoT device with local storage | wlite | Minimal dependencies |
| Build-time schema generation | wlite | Fast, no interpreter overhead |
| Database migration scripts | Either | Both output plain SQL |
| Schema version control | Either | Both produce deterministic output |
| Multi-database project | dbwarden | Only tool with multi-db support |
| Cross-language project | wlite | Bindings for 6+ languages |
| Prototyping in Python | dbwarden | Quick iteration with SQLAlchemy |
| Production embedded system | wlite | Stable ABI, predictable behavior |
| Team with mixed language skills | wlite | Everyone can use the same tool |

## Performance comparison

Performance characteristics differ between the two tools due to their
implementation languages and runtime requirements.

### Startup time

wlite starts in microseconds. The C binary loads and begins executing
immediately. dbwarden requires Python interpreter startup, SQLAlchemy
import, and module resolution, which typically takes hundreds of milliseconds.

For CLI tools that are invoked frequently (such as in build scripts or
pre-commit hooks), this difference is significant. A build step that invokes
wlite 50 times per build will complete much faster than the same step using
dbwarden.

### Memory usage

libwlite uses a few megabytes of memory for typical schema operations.
dbwarden's Python runtime, SQLAlchemy ORM, and module imports typically
consume tens of megabytes. For memory-constrained environments such as
embedded systems or containers with tight limits, this difference matters.

### Schema compilation speed

For simple schemas (under 50 tables), both tools compile schemas in under a
second. The difference is negligible for most applications.

For complex schemas (hundreds of tables), wlite's C implementation is
measurably faster. The collapse logic in libwlite is optimized for large
numbers of concurrent table rebuilds, and the C memory model avoids the
overhead of Python object allocation and garbage collection.

### SQL generation

Both tools generate SQL character by character. The output is identical for
the same input schema. The speed of generation is similar for both tools
because the bottleneck is the diff algorithm, not the string operations.

The only difference is in whitespace formatting. dbwarden uses Python string
formatting which may introduce minor whitespace variations. libwlite uses a
custom string builder that produces consistent output. Both tools normalize
their output before comparison in the CI sync.

## Migration between tools

### Migrating from dbwarden to wlite

If you are using dbwarden for SQLite-only projects and want to remove the
Python dependency, migrating to wlite is straightforward.

**Step 1**: Export your SQLAlchemy models to `.wlite` format. dbwarden
provides a `dbwarden export wlite` command that reads your models and writes
a `.wlite` file.

**Step 2**: Verify that the `.wlite` file produces equivalent SQL. Run
`dbwarden compile` and `wlite compile` on the same schema and compare the
output. The CI sync ensures that the output is identical, but manual
verification is good practice.

**Step 3**: Replace your dbwarden CLI invocations with wlite CLI
invocations. The command syntax is similar but not identical. Check the wlite
CLI reference for the specific commands you use.

**Step 4**: Update your CI pipeline to use wlite instead of dbwarden for
SQLite schema tasks. Remove the Python and SQLAlchemy dependencies if they
are no longer needed.

**Step 5**: Test the migration on a non-production database before deploying
to production.

### Migrating from wlite to dbwarden

If your project grows to need PostgreSQL support or SQLAlchemy integration,
migrating from wlite to dbwarden is also straightforward.

**Step 1**: Import your `.wlite` files into dbwarden. dbwarden reads `.wlite`
files natively through its SQLite backend.

**Step 2**: Add SQLAlchemy models for any new database targets (PostgreSQL,
MySQL, etc.). dbwarden's model layer lets you define schemas in Python while
using the same `.wlite` files for SQLite.

**Step 3**: Update your CI pipeline to use dbwarden. The wlite CLI can
coexist with dbwarden if you need a gradual migration.

**Step 4**: Remove wlite-specific tooling from your build process once
dbwarden handles all your schema needs.

### When migration does not make sense

Migration is not always the right choice. If you are using dbwarden for
multi-database projects, stick with dbwarden. If you are using wlite in
embedded systems, stick with wlite. The tools are complementary, not
competing. Use the one that fits your constraints.

## Contributing to both projects

### Shared contribution guidelines

Both projects follow the same code style for schema-related logic. When
contributing a change that affects the shared core, you must update both
repositories.

The recommended workflow for shared changes:

1. Implement the change in dbwarden first (Python is faster to iterate in)
2. Run dbwarden's test suite to verify correctness
3. Port the change to libwlite (C)
4. Run libwlite's test suite to verify equivalence
5. Run the CI sync to confirm that the outputs match
6. Submit pull requests to both repositories

This workflow ensures that the change is verified in both implementations
before it lands.

### Project-specific contributions

For changes that affect only one project, follow the contribution guidelines
of that project.

dbwarden-specific changes (Python, SQLAlchemy, plugins, async drivers) go
only to the dbwarden repository. These changes do not affect the shared core
and do not require a corresponding libwlite change.

wlite-specific changes (C library API, language bindings, CLI features, TUI)
go only to the wlite repository. These changes do not affect the shared core
and do not require a corresponding dbwarden change.

### Testing shared changes

When testing a shared change, you must verify that both implementations
produce identical SQL output for the same input. The CI sync automates this
verification, but manual testing during development is important.

A good testing practice is to create a reference schema that exercises the
change, compile it with both tools, and diff the output. If the output
matches, the change is consistent across both implementations.

### Code review

Both projects require code review before merging. For shared changes, both
maintainers should review the change. For project-specific changes, only the
maintainers of that project need to review.

The review checklist for shared changes includes:

- Does the change affect the schema diff algorithm?
- Does the change affect type normalization?
- Does the change affect the rebuild sequence?
- Does the change affect collapse logic?
- Does the change affect constraint diffing?
- Is the SQL output identical between both implementations?
- Are there new test cases for both implementations?
- Does the CI sync pass?

## Future directions

### dbwarden roadmap

dbwarden's future development focuses on expanding database support and
improving the plugin ecosystem. Planned work includes:

- Materialized view management for PostgreSQL
- Enhanced seed data management with conditional inserts
- Improved impact analysis for large schema changes
- Better integration with Alembic for hybrid migration workflows
- Support for database-specific features (PARTITION BY, CLUSTER, etc.)
- Enhanced async support with connection pooling

### wlite roadmap

wlite's future development focuses on portability, performance, and language
bindings. Planned work includes:

- WASM compilation for browser-based schema tools
- Additional language bindings (Swift, Java, Kotlin)
- Improved diff algorithm for very large schemas (1000+ tables)
- Schema visualization output (DOT format for Graphviz)
- Incremental compilation (only rebuild changed tables)
- Better error messages with schema location information
- Support for SQLite extensions (FTS5, R-Tree, JSON)

### Shared future work

Some work benefits both projects and will be developed in coordination:

- Improved collapse algorithm that handles cross-table dependencies
- Better handling of views and triggers in schema diffing
- Support for SQLite's `INSERT OR REPLACE` semantics in seed data
- Schema validation that catches common mistakes before compilation
- Performance benchmarks that track both implementations over time

### Long-term vision

The long-term vision is that dbwarden and wlite form a complete schema
management solution for any project. If you use Python and SQLAlchemy, use
dbwarden. If you use C, Rust, Go, or any other language, use wlite. If you
use both, use the `.wlite` format as your single source of truth and compile
it with whichever tool fits each context.

The shared core ensures that your schemas behave identically regardless of
which tool compiles them. The CI sync ensures that the two tools never drift
apart. The result is a schema management ecosystem that scales from embedded
firmware to enterprise applications.

## Summary

| Aspect | dbwarden | wlite |
|--------|----------|-------|
| Purpose | Multi-database schema compiler | SQLite-only schema compiler |
| Implementation | Python | C |
| Schema format | SQLAlchemy models, .wlite | .wlite |
| Shared core | SQLite3 backend | libwlite |
| CI sync | Yes | Yes |
| License | MIT | MIT |

dbwarden and wlite share a common foundation in SQLite schema management. They
diverge in their scope, implementation language, and target audience. The CI
synchronization ensures that they stay aligned on shared behavior. Choose the
tool that fits your constraints, and know that the other tool will produce the
same results for the same schema.
