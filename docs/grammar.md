---
title: .wlite Grammar
description: Complete grammar specification for the .wlite model format with examples.
---

# .wlite Grammar Specification

The `.wlite` format is a declarative schema description language. You define the tables, fields, constraints, indexes, and other database objects you want. libwlite reads the model, compares it against the live database, and generates the SQL to reconcile them.

## File structure

A `.wlite` file is a sequence of top-level declarations. Declarations can appear in any order. Whitespace and comments are ignored between declarations.

```
file := top_level*
```

## Top-level declarations

```
top_level := model_decl
           | model_config
           | index_decl
           | view_decl
           | trigger_decl
```

A single `.wlite` file can contain any number of models, indexes, views, and triggers. The `model_config` block is optional and appears at most once.

## Model config

The `model_config` block sets metadata for the entire model file. It is optional.

```
model_config := 'model_config' '{' config_entry* '}'
config_entry := 'name' STRING | 'version' NUMBER
```

| Field | Type | Description |
|-------|------|-------------|
| `name` | STRING | Application or schema name |
| `version` | NUMBER | Schema version number |

Example:

```
model_config {
    name "my_application"
    version 3
}
```

The version number is informational. It does not affect migration behavior but is useful for tracking schema evolution.

## Model (table)

A `model` block defines a table. The model name is a PascalCase identifier. The `table` option sets the actual SQLite table name (if omitted, the model name is lowercased).

```
model_decl := 'model' IDENT '{' model_body '}'
model_body := model_option* field_decl* constraint_decl*
model_option := 'table' STRING
             | 'strict'
             | 'without' 'rowid'
             | 'comment' STRING
```

### Model options

| Option | Description |
|--------|-------------|
| `table "name"` | Explicit SQLite table name (default: lowercased model name) |
| `strict` | Enable STRICT mode (SQLite 3.37+) |
| `without rowid` | Create WITHOUT ROWID table |
| `comment "text"` | Table comment (stored as `COMMENT ON TABLE`) |

### Example: basic model

```
model User {
    table "users"
    comment "Core user accounts"

    field id integer {
        primary_key
        autoincrement
    }

    field username text {
        not_null
        unique
    }

    field email text

    field active boolean {
        not_null
        default true
    }

    field created_at datetime {
        not_null
        default CURRENT_TIMESTAMP
    }
}
```

### Example: strict table

```
model Session {
    table "sessions"
    strict

    field id integer {
        primary_key
        autoincrement
    }

    field user_id integer {
        not_null
        references User.id
    }

    field token text {
        not_null
        unique
    }

    field expires_at datetime {
        not_null
    }
}
```

STRICT mode enforces that every column must have a type. It also disables implicit type affinity rules, making behavior more predictable.

### Example: without rowid

```
model UserPreference {
    table "user_preferences"
    without rowid

    field user_id integer {
        primary_key
        references User.id
    }

    field key text {
        primary_key
    }

    field value text
}
```

WITHOUT ROWID tables are smaller and faster for tables with a composite primary key and no integer primary key.

## Field declarations

A field declaration defines a column. The type is one of the supported SQLite types (see [Types](#types)). Attributes follow the type in braces.

```
field_decl := 'field' IDENT TYPE field_attr*
field_attr := 'primary_key'
            | 'autoincrement'
            | 'not_null'
            | 'unique'
            | 'default' EXPR
            | 'collate' IDENT
            | 'references' IDENT '.' IDENT
            | 'generated' '(' EXPR ')' ('stored' | 'virtual')
```

### Field attributes

| Attribute | Description |
|-----------|-------------|
| `primary_key` | Column is part of the primary key |
| `autoincrement` | SQLite AUTOINCREMENT (only valid with `primary_key` on a single integer column) |
| `not_null` | Column cannot be NULL |
| `unique` | Values must be unique across all rows |
| `default EXPR` | Default value when not specified in INSERT |
| `collate NAME` | Collation sequence (e.g., `NOCASE`, `RTRIM`) |
| `references Table.column` | Foreign key reference |
| `generated (EXPR) stored` | Stored generated column (computed on write) |
| `generated (EXPR) virtual` | Virtual generated column (computed on read) |

### Example: all attributes

```
model Product {
    table "products"

    field id integer {
        primary_key
        autoincrement
    }

    field name text {
        not_null
        collate NOCASE
    }

    field slug text {
        not_null
        unique
    }

    field price real {
        not_null
        default 0.0
    }

    field sku text {
        unique
    }

    field search_name text {
        generated (lower(name)) stored
    }

    field category_id integer {
        references Category.id
    }
}
```

### Example: generated columns

```
model Order {
    table "orders"

    field id integer {
        primary_key
        autoincrement
    }

    field quantity integer {
        not_null
    }

    field unit_price real {
        not_null
    }

    field total real {
        generated (quantity * unit_price) stored
    }
}
```

Stored generated columns are computed on write and stored on disk. Virtual generated columns are computed on read and not stored.

## Table constraints

Table-level constraints define composite primary keys, unique constraints, foreign keys, and check constraints.

```
constraint_decl := 'primary_key' '(' cols ')'
                 | 'unique' '(' cols ')'
                 | 'check' '(' EXPR ')'
                 | 'foreign_key' '(' cols ')' 'references' IDENT '(' cols ')' fk_action*
fk_action := 'on' ('delete' | 'update') FK_ACTION
FK_ACTION := 'cascade' | 'restrict' | 'set null' | 'set default' | 'no action'
```

### Example: composite primary key

```
model OrderItem {
    table "order_items"

    field order_id integer { not_null }
    field product_id integer { not_null }
    field quantity integer { not_null }

    primary_key (order_id, product_id)
}
```

### Example: composite unique

```
model UserEmail {
    table "user_emails"

    field user_id integer { not_null }
    field email text { not_null }
    field is_primary boolean { not_null default true }

    unique (user_id, email)
}
```

### Example: foreign key with actions

```
model Comment {
    table "comments"

    field id integer {
        primary_key
        autoincrement
    }

    field post_id integer {
        not_null
        references Post.id
        on delete cascade
    }

    field user_id integer {
        not_null
        references User.id
        on delete restrict
        on update cascade
    }

    field body text { not_null }
}
```

### Example: check constraint

```
model Event {
    table "events"

    field id integer {
        primary_key
        autoincrement
    }

    field start_time datetime { not_null }
    field end_time datetime { not_null }

    check (end_time > start_time)
}
```

## Indexes

Index declarations define database indexes. Indexes are created after the tables they reference.

```
index_decl := 'index' IDENT '{' index_body '}'
index_body := ['unique'] 'on' IDENT '(' index_cols ')' ['where' EXPR]
index_cols := (IDENT | '(' EXPR ')') (',' ...)*
```

### Example: simple index

```
index users_email {
    on users(email)
}
```

### Example: unique index

```
index users_username {
    unique
    on users(username)
}
```

### Example: partial index

```
index active_users {
    on users(email)
    where active = true
}
```

Partial indexes are smaller and faster because they only index rows matching the `WHERE` clause.

### Example: composite index

```
index order_lookup {
    on order_items(order_id, product_id)
}
```

## Views

Views are virtual tables defined by a SELECT query.

```
view_decl := 'view' IDENT '{' 'select' EXPR '}'
```

### Example

```
view active_users {
    select
        id, username, email
    from users
    where active = true
}
```

## Triggers

Triggers execute SQL in response to data changes.

```
trigger_decl := 'trigger' IDENT '{' timing event 'on' IDENT body '}'
timing := 'before' | 'after' | 'instead' 'of'
event := 'insert' | 'update' ['of' IDENT+ ] | 'delete'
body := 'begin' EXPR 'end'
```

### Example: before insert

```
trigger set_user_timestamps {
    before insert on users
    begin
        update users set created_at = CURRENT_TIMESTAMP where id = NEW.id
    end
}
```

### Example: after update

```
trigger audit_email_changes {
    after update of email on users
    begin
        insert into audit_log (user_id, old_email, new_email, changed_at)
        values (OLD.id, OLD.email, NEW.email, CURRENT_TIMESTAMP)
    end
}
```

## Types

SQLite uses type affinity to determine how values are stored. The `.wlite` format supports the following types:

| Type | SQLite affinity | Notes |
|------|-----------------|-------|
| `integer` | INTEGER | Whole numbers |
| `real` | REAL | Floating point |
| `text` | TEXT | Strings |
| `blob` | BLOB | Binary data |
| `boolean` | INTEGER | Stored as 0/1 |
| `date` | TEXT | ISO 8601 date string |
| `datetime` | TEXT | ISO 8601 datetime string |
| `uuid` | TEXT | UUID string |
| `json` | TEXT | JSON string |

Any other type name is preserved as-is. This lets you use SQLite type affinity tricks (e.g., `VARCHAR(255)`) even though SQLite ignores length constraints.

### Type normalization

When comparing schemas, libwlite normalizes types:

- `INT`, `INTEGER`, `INT4`, `SIGNED` are equivalent
- `BOOLEAN`, `BOOL`, `TINYINT` map to `INTEGER`
- `DATETIME`, `TIMESTAMP` map to `TEXT`
- `NUMERIC`, `DECIMAL`, `DOUBLE`, `FLOAT` map to `REAL`

This means changing `INT` to `INTEGER` in your model does not trigger a migration.

## Comments

```
# This is a line comment
/* This is a block comment */
```

Comments can appear anywhere whitespace is allowed. They are ignored by the parser.

## Full example

Here is a complete `.wlite` model for a blog application:

```
model_config {
    name "blog"
    version 1
}

model User {
    table "users"
    strict
    comment "User accounts"

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
        unique
    }

    field password_hash text {
        not_null
    }

    field is_admin boolean {
        not_null
        default false
    }

    field created_at datetime {
        not_null
        default CURRENT_TIMESTAMP
    }

    field updated_at datetime {
        not_null
        default CURRENT_TIMESTAMP
    }
}

model Post {
    table "posts"
    strict
    comment "Blog posts"

    field id integer {
        primary_key
        autoincrement
    }

    field title text {
        not_null
    }

    field slug text {
        not_null
        unique
    }

    field body text {
        not_null
    }

    field author_id integer {
        not_null
        references User.id
        on delete cascade
    }

    field published boolean {
        not_null
        default false
    }

    field created_at datetime {
        not_null
        default CURRENT_TIMESTAMP
    }

    field updated_at datetime {
        not_null
        default CURRENT_TIMESTAMP
    }
}

model Comment {
    table "comments"
    strict

    field id integer {
        primary_key
        autoincrement
    }

    field post_id integer {
        not_null
        references Post.id
        on delete cascade
    }

    field author_id integer {
        not_null
        references User.id
        on delete cascade
    }

    field body text {
        not_null
    }

    field created_at datetime {
        not_null
        default CURRENT_TIMESTAMP
    }
}

index posts_author {
    on posts(author_id)
}

index posts_slug {
    unique
    on posts(slug)
}

index comments_post {
    on comments(post_id)
}

index comments_author {
    on comments(author_id)
}
```

## What wlite generates

Given the model above on an empty database, `wlite migrate` produces:

```sql
-- upgrade
CREATE TABLE IF NOT EXISTS users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT NOT NULL UNIQUE,
    email TEXT NOT NULL UNIQUE,
    password_hash TEXT NOT NULL,
    is_admin BOOLEAN NOT NULL DEFAULT FALSE,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
);
COMMENT ON TABLE users IS 'User accounts';

CREATE TABLE IF NOT EXISTS posts (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    title TEXT NOT NULL,
    slug TEXT NOT NULL UNIQUE,
    body TEXT NOT NULL,
    author_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    published BOOLEAN NOT NULL DEFAULT FALSE,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
);
COMMENT ON TABLE posts IS 'Blog posts';

CREATE TABLE IF NOT EXISTS comments (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    post_id INTEGER NOT NULL REFERENCES posts(id) ON DELETE CASCADE,
    author_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    body TEXT NOT NULL,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS posts_author ON posts (author_id);
CREATE UNIQUE INDEX IF NOT EXISTS posts_slug ON posts (slug);
CREATE INDEX IF NOT EXISTS comments_post ON comments (post_id);
CREATE INDEX IF NOT EXISTS comments_author ON comments (author_id);
```

When you later add a field to the `Post` model, wlite detects the difference and generates only the `ALTER TABLE` needed to add it.
