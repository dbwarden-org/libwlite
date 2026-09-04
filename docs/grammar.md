---
title: .wlite Grammar
description: Formal grammar specification for the .wlite model format.
---

# .wlite Grammar Specification

## File Structure

```
file := top_level*
```

## Top-level Declarations

```
top_level := model_decl
           | model_config
           | index_decl
           | view_decl
           | trigger_decl
           | database_decl
```

## Model Config

```
model_config := 'model_config' '{' config_entry* '}'
config_entry := 'name' STRING | 'version' NUMBER
```

Example:

```
model_config {
    name "my_application"
    version 3
}
```

## Model (Table)

```
model_decl := 'model' IDENT '{' model_body '}'
model_body := model_option* field_decl* constraint_decl*
model_option := 'table' STRING
             | 'strict'
             | 'without' 'rowid'
             | 'comment' STRING
```

Example:

```
model User {
    table "users"
    strict

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
}
```

## Field Declarations

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

## Table Constraints

```
constraint_decl := 'primary_key' '(' cols ')'
                 | 'unique' '(' cols ')'
                 | 'check' '(' EXPR ')'
                 | 'foreign_key' '(' cols ')' 'references' IDENT '(' cols ')' fk_action*
fk_action := 'on' ('delete' | 'update') FK_ACTION
FK_ACTION := 'cascade' | 'restrict' | 'set null' | 'set default' | 'no action'
```

## Indexes

```
index_decl := 'index' IDENT '{' index_body '}'
index_body := ['unique'] 'on' IDENT '(' index_cols ')' ['where' EXPR]
index_cols := (IDENT | '(' EXPR ')') (',' ...)*
```

Example:

```
index users_email {
    unique
    on users(email)
    where deleted_at IS NULL
}
```

## Views

```
view_decl := 'view' IDENT '{' 'select' EXPR '}'
```

## Triggers

```
trigger_decl := 'trigger' IDENT '{' timing event 'on' IDENT body '}'
timing := 'before' | 'after' | 'instead' 'of'
event := 'insert' | 'update' ['of' IDENT+ ] | 'delete'
body := 'begin' EXPR 'end'
```

## Types

Supported types: `integer`, `real`, `text`, `blob`, `boolean`, `date`, `datetime`, `uuid`, `json`, and any arbitrary type name (preserved as-is).

## Comments

```
# This is a line comment
/* This is a block comment */
```
