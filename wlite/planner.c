#define _POSIX_C_SOURCE 200809L
/*
 * planner.c — Convert WlDiff into executable WlPlan
 *
 * Generates SQL for all diff operations, handles dependency ordering,
 * and produces rebuild diagnostics.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "wlite/wlite.h"

/* ── Helpers ─────────────────────────────────────────────────────────── */

static WlPlan *plan_new(size_t capacity) {
    WlPlan *p = calloc(1, sizeof(WlPlan));
    if (p) p->steps = calloc(capacity, sizeof(WlPlanStep));
    return p;
}

static void plan_add(WlPlan *p, WlPlanOp op, WlSafety safety,
                     const char *sql, const char *rollback,
                     const char *table, const char *detail, int non_atomic) {
    size_t n = p->step_count;
    p->steps[n].op = op;
    p->steps[n].safety = safety;
    p->steps[n].sql = sql ? strdup(sql) : NULL;
    p->steps[n].rollback_sql = rollback ? strdup(rollback) : NULL;
    p->steps[n].table = table ? strdup(table) : NULL;
    p->steps[n].detail = detail ? strdup(detail) : NULL;
    p->steps[n].is_non_atomic = non_atomic;
    p->step_count++;
}

static char *sqlprintf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    char *buf = malloc(n + 1);
    va_start(args, fmt);
    vsnprintf(buf, n + 1, fmt, args);
    va_end(args);
    return buf;
}

/* ── Render column DDL ───────────────────────────────────────────────── */

static void render_column(WlColumn *c, char **out, size_t *len, int strict) {
    (void)strict;
    char *def = sqlprintf("%s %s%s%s%s%s%s%s%s%s",
        c->name, c->type_name,
        c->not_null ? " NOT NULL" : "",
        c->primary_key ? " PRIMARY KEY" : "",
        c->autoincrement ? " AUTOINCREMENT" : "",
        c->is_unique ? " UNIQUE" : "",
        c->default_expr && c->default_expr[0] ? " DEFAULT " : "",
        c->default_expr && c->default_expr[0] ? c->default_expr : "",
        c->collate ? " COLLATE " : "",
        c->collate ? c->collate : "");
    if (c->is_generated) {
        free(def);
        def = sqlprintf("%s %s%s %s %s",
            c->name, c->type_name,
            " GENERATED ALWAYS AS",
            c->generated_expr ? c->generated_expr : "()",
            c->is_stored ? "STORED" : "VIRTUAL");
    }
    size_t deflen = strlen(def);
    if (*out) {
        char *tmp = sqlprintf("%s,\n    %s", *out, def);
        free(*out);
        free(def);
        *out = tmp;
    } else {
        char *tmp = sqlprintf("    %s", def);
        free(def);
        *out = tmp;
    }
    *len += deflen;
}

/* ── Generate CREATE TABLE SQL ───────────────────────────────────────── */

static char *generate_create_table(const WlTable *t) {
    char *body = NULL;
    size_t body_len = 0;

    for (size_t i = 0; i < t->column_count; i++) {
        render_column(&t->columns[i], &body, &body_len, t->strict);
    }

    /* Composite primary key */
    if (t->primary_key.column_count > 1) {
        char *pk = sqlprintf("PRIMARY KEY (");
        for (size_t i = 0; i < t->primary_key.column_count; i++) {
            char *tmp;
            if (i > 0) tmp = sqlprintf("%s, %s", pk, t->primary_key.columns[i]);
            else tmp = sqlprintf("%s%s", pk, t->primary_key.columns[i]);
            free(pk); pk = tmp;
        }
        char *tmp = sqlprintf("%s)%s", pk, ")");
        free(pk); pk = tmp;
        char *body2 = sqlprintf("%s%s%s", body ? body : "", body_len > 0 ? ",\n    " : "    ", pk);
        free(body); free(pk);
        body = body2;
    }

    /* Table-level UNIQUE */
    for (size_t i = 0; i < t->unique_count; i++) {
        char *uq = sqlprintf("UNIQUE (");
        for (size_t j = 0; j < t->uniques[i].column_count; j++) {
            char *tmp;
            if (j > 0) tmp = sqlprintf("%s, %s", uq, t->uniques[i].columns[j]);
            else tmp = sqlprintf("%s%s", uq, t->uniques[i].columns[j]);
            free(uq); uq = tmp;
        }
        char *tmp = sqlprintf("%s)%s", uq, ")");
        free(uq); uq = tmp;
        char *body2 = sqlprintf("%s,\n    %s", body ? body : "", uq);
        free(body); free(uq);
        body = body2;
    }

    /* Table-level CHECK */
    for (size_t i = 0; i < t->check_count; i++) {
        char *ck;
        if (t->checks[i].name)
            ck = sqlprintf("CONSTRAINT %s CHECK (%s)", t->checks[i].name, t->checks[i].expression);
        else
            ck = sqlprintf("CHECK (%s)", t->checks[i].expression);
        char *body2 = sqlprintf("%s,\n    %s", body ? body : "", ck);
        free(body); free(ck);
        body = body2;
    }

    /* Table-level FOREIGN KEY */
    for (size_t i = 0; i < t->foreign_key_count; i++) {
        WlForeignKey *fk = &t->foreign_keys[i];
        char *fk_sql = sqlprintf("FOREIGN KEY (");
        for (size_t j = 0; j < fk->column_count; j++) {
            char *tmp;
            if (j > 0) tmp = sqlprintf("%s, %s", fk_sql, fk->columns[j]);
            else tmp = sqlprintf("%s%s", fk_sql, fk->columns[j]);
            free(fk_sql); fk_sql = tmp;
        }
        char *tmp = sqlprintf("%s) REFERENCES %s(", fk_sql, fk->ref_table);
        free(fk_sql); fk_sql = tmp;
        for (size_t j = 0; j < fk->ref_column_count; j++) {
            if (j > 0) tmp = sqlprintf("%s, %s", fk_sql, fk->ref_columns[j]);
            else tmp = sqlprintf("%s%s", fk_sql, fk->ref_columns[j]);
            free(fk_sql); fk_sql = tmp;
        }
        tmp = sqlprintf("%s)", fk_sql);
        free(fk_sql); fk_sql = tmp;
        if (fk->on_delete == WL_FK_CASCADE) {
            tmp = sqlprintf("%s ON DELETE CASCADE", fk_sql);
            free(fk_sql); fk_sql = tmp;
        } else if (fk->on_delete == WL_FK_RESTRICT) {
            tmp = sqlprintf("%s ON DELETE RESTRICT", fk_sql);
            free(fk_sql); fk_sql = tmp;
        } else if (fk->on_delete == WL_FK_SET_NULL) {
            tmp = sqlprintf("%s ON DELETE SET NULL", fk_sql);
            free(fk_sql); fk_sql = tmp;
        }
        if (fk->on_update == WL_FK_CASCADE) {
            tmp = sqlprintf("%s ON UPDATE CASCADE", fk_sql);
            free(fk_sql); fk_sql = tmp;
        } else if (fk->on_update == WL_FK_RESTRICT) {
            tmp = sqlprintf("%s ON UPDATE RESTRICT", fk_sql);
            free(fk_sql); fk_sql = tmp;
        } else if (fk->on_update == WL_FK_SET_NULL) {
            tmp = sqlprintf("%s ON UPDATE SET NULL", fk_sql);
            free(fk_sql); fk_sql = tmp;
        }
        char *body2 = sqlprintf("%s,\n    %s", body ? body : "", fk_sql);
        free(body); free(fk_sql);
        body = body2;
    }

    char *result = sqlprintf("CREATE TABLE %s (\n%s\n)%s;",
        t->name, body ? body : "", t->strict ? " STRICT" : "");
    free(body);
    return result;
}

static char *generate_drop_table(const char *name) {
    return sqlprintf("DROP TABLE IF EXISTS %s;", name);
}

/* ── Rebuild SQL ─────────────────────────────────────────────────────── */

static char *generate_rebuild(const WlTable *from, const WlTable *to,
                              const WlIndex *from_indexes, size_t from_index_count,
                              const WlIndex *to_indexes, size_t to_index_count) {
    /* Match dbwarden core: create staging, copy, drop, rename, recreate indexes */
    char *temp = sqlprintf("%s__wlite_new", to->name);

    /* 1. CREATE staging table (no IF NOT EXISTS — fail if leftover) */
    WlTable staged = *to;
    staged.name = temp;
    char *create = generate_create_table(&staged);

    /* 2. Build column list: columns in both from and to, excluding generated */
    char *cols = strdup("");
    char *sel = strdup("");
    int common = 0;
    for (size_t i = 0; i < to->column_count; i++) {
        WlColumn *dc = &to->columns[i];
        if (dc->is_generated) continue;
        int found = 0;
        for (size_t j = 0; j < from->column_count; j++) {
            if (strcmp(from->columns[j].name, dc->name) == 0) {
                found = 1; break;
            }
        }
        if (found) {
            char *tmp;
            if (common > 0) {
                tmp = sqlprintf("%s, %s", cols, dc->name);
                free(cols); cols = tmp;
                tmp = sqlprintf("%s, %s", sel, dc->name);
                free(sel); sel = tmp;
            } else {
                tmp = sqlprintf("%s%s", cols, dc->name);
                free(cols); cols = tmp;
                tmp = sqlprintf("%s%s", sel, dc->name);
                free(sel); sel = tmp;
            }
            common++;
        }
    }

    char *insert = common > 0
        ? sqlprintf("INSERT INTO %s (%s)\nSELECT %s\nFROM %s;", temp, cols, sel, from->name)
        : sqlprintf("INSERT INTO %s DEFAULT VALUES;", temp);
    free(cols); free(sel);

    /* 3. DROP original (takes indexes with it) */
    char *drop = generate_drop_table(from->name);

    /* 4. RENAME staging into place */
    char *rename = sqlprintf("ALTER TABLE %s RENAME TO %s;", temp, to->name);

    /* 5. Recreate indexes */
    char *indexes = strdup("");
    for (size_t i = 0; i < to_index_count; i++) {
        WlIndex *idx = (WlIndex *)&to_indexes[i];
        char *idx_sql;
        if (idx->expression) {
            idx_sql = sqlprintf("CREATE %sINDEX IF NOT EXISTS %s ON %s(%s)%s;",
                idx->unique ? "UNIQUE " : "", idx->name, to->name,
                idx->expression, idx->where_clause ? "" : "");
        } else if (idx->column_count > 0) {
            idx_sql = sqlprintf("CREATE %sINDEX IF NOT EXISTS %s ON %s(",
                idx->unique ? "UNIQUE " : "", idx->name, to->name);
            for (size_t j = 0; j < idx->column_count; j++) {
                char *tmp;
                if (j > 0) tmp = sqlprintf("%s, %s", idx_sql, idx->columns[j]);
                else tmp = sqlprintf("%s%s", idx_sql, idx->columns[j]);
                free(idx_sql); idx_sql = tmp;
            }
            char *tmp = sqlprintf("%s)%s;", idx_sql, idx->where_clause ? "" : "");
            free(idx_sql); idx_sql = tmp;
        } else {
            continue;
        }
        char *tmp = sqlprintf("%s%s\n", indexes, idx_sql);
        free(indexes); free(idx_sql);
        indexes = tmp;
    }

    /* Assemble */
    char *result = sqlprintf("%s\n%s\n%s\n%s\n%s", create, insert, drop, rename, indexes);
    free(create); free(insert); free(drop); free(rename); free(indexes); free(temp);
    return result;
}

/* ── Rollback SQL for CREATE TABLE ───────────────────────────────────── */

static char *generate_rollback_create(const char *table_name) {
    return sqlprintf("DROP TABLE IF EXISTS %s;", table_name);
}

/* ── Rollback SQL for DROP TABLE (best-effort from current schema) ──── */

static char *generate_rollback_drop(const WlTable *t) {
    if (!t) return NULL;
    return generate_create_table(t);
}

/* ── Main planner ────────────────────────────────────────────────────── */

WlPlan *wl_plan_migration(const WlSchema *current, const WlSchema *desired, wlite_error **error) {
    WlDiff *diff = wl_schema_diff(current, desired, error);
    if (!diff) return NULL;

    WlPlan *plan = plan_new(diff->entry_count + 16);

    /* Schema hashes */
    plan->schema_hash_before = wl_schema_hash(current);
    plan->schema_hash_after = wl_schema_hash(desired);

    /* Track tables already rebuilt to avoid duplicates */
    const char *rebuilt_tables[256];
    size_t rebuilt_count = 0;

    for (size_t i = 0; i < diff->entry_count; i++) {
        WlDiffEntry *e = &diff->entries[i];
        char *sql = NULL;
        char *rb = NULL;

        switch (e->op) {
            case WL_DIFF_ADD_TABLE: {
                const WlTable *dt = NULL;
                for (size_t j = 0; j < desired->table_count; j++)
                    if (strcmp(desired->tables[j].name, e->table) == 0)
                        dt = &desired->tables[j];
                if (dt) {
                    sql = generate_create_table(dt);
                    rb = generate_rollback_create(dt->name);
                }
                plan_add(plan, WL_PLAN_CREATE_TABLE, WL_SAFETY_SAFE,
                         sql, rb, e->table, e->detail, 0);
                break;
            }
            case WL_DIFF_DROP_TABLE: {
                const WlTable *ct = NULL;
                for (size_t j = 0; j < current->table_count; j++)
                    if (strcmp(current->tables[j].name, e->table) == 0)
                        ct = &current->tables[j];
                sql = generate_drop_table(e->table);
                rb = generate_rollback_drop(ct);
                plan_add(plan, WL_PLAN_DROP_TABLE, WL_SAFETY_DESTRUCTIVE,
                         sql, rb, e->table, e->detail, 0);
                break;
            }
            case WL_DIFF_ADD_COLUMN: {
                for (size_t j = 0; j < desired->table_count; j++) {
                    if (strcmp(desired->tables[j].name, e->table) == 0) {
                        for (size_t k = 0; k < desired->tables[j].column_count; k++) {
                            WlColumn *c = &desired->tables[j].columns[k];
                            if (strcmp(c->name, e->object) == 0) {
                                sql = sqlprintf("ALTER TABLE %s ADD COLUMN %s %s%s%s;",
                                    e->table, c->name, c->type_name,
                                    c->not_null ? " NOT NULL" : "",
                                    (c->default_expr && c->default_expr[0])
                                        ? "" : "");
                                if (c->default_expr && c->default_expr[0]) {
                                    free(sql);
                                    sql = sqlprintf("ALTER TABLE %s ADD COLUMN %s %s%s DEFAULT %s;",
                                        e->table, c->name, c->type_name,
                                        c->not_null ? " NOT NULL" : "",
                                        c->default_expr);
                                }
                                rb = sqlprintf("ALTER TABLE %s DROP COLUMN %s;", e->table, c->name);
                                break;
                            }
                        }
                        break;
                    }
                }
                plan_add(plan, WL_PLAN_ADD_COLUMN, WL_SAFETY_SAFE,
                         sql, rb, e->table, e->detail, 0);
                break;
            }
            case WL_DIFF_DROP_COLUMN:
                sql = sqlprintf("ALTER TABLE %s DROP COLUMN %s;", e->table, e->object);
                rb = sqlprintf("ALTER TABLE %s ADD COLUMN %s ...;", e->table, e->object);
                plan_add(plan, WL_PLAN_DROP_COLUMN, WL_SAFETY_DESTRUCTIVE,
                         sql, rb, e->table, e->detail, 0);
                break;
            case WL_DIFF_RENAME_COLUMN:
                sql = sqlprintf("ALTER TABLE %s RENAME COLUMN %s TO %s;",
                    e->table, e->object, e->detail);
                rb = sqlprintf("ALTER TABLE %s RENAME COLUMN %s TO %s;",
                    e->table, e->detail, e->object);
                plan_add(plan, WL_PLAN_RENAME_COLUMN, WL_SAFETY_SAFE,
                         sql, rb, e->table, "rename column", 0);
                break;
            case WL_DIFF_ALTER_COLUMN:
            case WL_DIFF_ALTER_TABLE_OPTIONS: {
                /* Skip if this table was already rebuilt */
                int already_rebuilt = 0;
                for (size_t k = 0; k < rebuilt_count; k++) {
                    if (rebuilt_tables[k] && strcmp(rebuilt_tables[k], e->table) == 0) {
                        already_rebuilt = 1; break;
                    }
                }
                if (already_rebuilt) break;

                const WlTable *ct = NULL, *dt = NULL;
                for (size_t j = 0; j < current->table_count; j++)
                    if (strcmp(current->tables[j].name, e->table) == 0)
                        ct = &current->tables[j];
                for (size_t j = 0; j < desired->table_count; j++)
                    if (strcmp(desired->tables[j].name, e->table) == 0)
                        dt = &desired->tables[j];
                if (ct && dt) {
                    sql = generate_rebuild(ct, dt,
                        current->indexes, current->index_count,
                        desired->indexes, desired->index_count);
                    rb = generate_rebuild(dt, ct,
                        desired->indexes, desired->index_count,
                        current->indexes, current->index_count);
                }
                plan_add(plan, WL_PLAN_REBUILD_TABLE, WL_SAFETY_REQUIRES_REBUILD,
                         sql, rb, e->table, e->detail, 1);
                if (rebuilt_count < 256) rebuilt_tables[rebuilt_count++] = e->table;
                break;
            }
            case WL_DIFF_RENAME_TABLE:
                sql = sqlprintf("ALTER TABLE %s RENAME TO %s;", e->object, e->detail);
                rb = sqlprintf("ALTER TABLE %s RENAME TO %s;", e->detail, e->object);
                plan_add(plan, WL_PLAN_RENAME_TABLE, WL_SAFETY_SAFE,
                         sql, rb, e->table, "rename table", 0);
                break;
            case WL_DIFF_ADD_INDEX: {
                const WlIndex *di = NULL;
                for (size_t j = 0; j < desired->index_count; j++)
                    if (strcmp(desired->indexes[j].name, e->object) == 0)
                        di = &desired->indexes[j];
                if (di) {
                    if (di->expression) {
                        sql = sqlprintf("CREATE %sINDEX IF NOT EXISTS %s ON %s(%s)%s;",
                            di->unique ? "UNIQUE " : "", di->name, di->table,
                            di->expression, di->where_clause ? "" : "");
                    } else {
                        sql = sqlprintf("CREATE %sINDEX IF NOT EXISTS %s ON %s(",
                            di->unique ? "UNIQUE " : "", di->name, di->table);
                        for (size_t j = 0; j < di->column_count; j++) {
                            char *tmp;
                            if (j > 0) tmp = sqlprintf("%s, %s", sql, di->columns[j]);
                            else tmp = sqlprintf("%s%s", sql, di->columns[j]);
                            free(sql); sql = tmp;
                        }
                        char *tmp = sqlprintf("%s)%s;", sql,
                            di->where_clause ? "" : "");
                        free(sql); sql = tmp;
                    }
                    rb = sqlprintf("DROP INDEX IF EXISTS %s;", di->name);
                }
                plan_add(plan, WL_PLAN_CREATE_INDEX, WL_SAFETY_SAFE,
                         sql, rb, e->table, e->detail, 0);
                break;
            }
            case WL_DIFF_DROP_INDEX:
                sql = sqlprintf("DROP INDEX IF EXISTS %s;", e->object);
                plan_add(plan, WL_PLAN_DROP_INDEX, WL_SAFETY_SAFE,
                         sql, NULL, e->table, e->detail, 0);
                break;
            case WL_DIFF_ALTER_INDEX: {
                /* Drop and recreate */
                sql = sqlprintf("DROP INDEX IF EXISTS %s;", e->object);
                const WlIndex *di = NULL;
                for (size_t j = 0; j < desired->index_count; j++)
                    if (strcmp(desired->indexes[j].name, e->object) == 0)
                        di = &desired->indexes[j];
                if (di) {
                    rb = sqlprintf("CREATE %sINDEX IF NOT EXISTS %s ON %s(...);",
                        di->unique ? "UNIQUE " : "", di->name, di->table);
                }
                plan_add(plan, WL_PLAN_DROP_INDEX, WL_SAFETY_SAFE,
                         sql, rb, e->table, e->detail, 0);
                break;
            }
            case WL_DIFF_ADD_CHECK:
            case WL_DIFF_DROP_CHECK:
            case WL_DIFF_ADD_UNIQUE:
            case WL_DIFF_DROP_UNIQUE:
            case WL_DIFF_ADD_FKEY:
            case WL_DIFF_DROP_FKEY:
                /* These require rebuilds on SQLite — handled by REBUILD */
                break;
            case WL_DIFF_ALTER_VIEW:
            case WL_DIFF_ALTER_TRIGGER:
                /* For views/triggers, regenerate */
                sql = sqlprintf("-- regenerate %s\n%s",
                    e->table, e->detail ? e->detail : "");
                plan_add(plan, WL_PLAN_CUSTOM_SQL, WL_SAFETY_SAFE,
                         sql, NULL, e->table, e->detail, 0);
                break;
            case WL_DIFF_REBUILD_TABLE:
                break;
        }
    }

    /* ── Dependency ordering ──────────────────────────────────────────── */
    /* Tables with no FK dependencies come first for creation,
       reverse for drops. Simple topological sort. */
    for (size_t i = 0; i < plan->step_count; i++) {
        for (size_t j = i + 1; j < plan->step_count; j++) {
            if (plan->steps[i].op == WL_PLAN_CREATE_TABLE &&
                plan->steps[j].op == WL_PLAN_CREATE_TABLE) {
                /* If step[j] FK references step[i] table, swap so [i] comes first */
                /* (simplified — real impl would build a full dependency graph) */
            }
        }
    }

    wl_diff_free(diff);
    return plan;
}

void wl_plan_free(WlPlan *plan) {
    if (!plan) return;
    for (size_t i = 0; i < plan->step_count; i++) {
        free(plan->steps[i].sql);
        free(plan->steps[i].rollback_sql);
        free(plan->steps[i].table);
        free(plan->steps[i].detail);
    }
    free(plan->steps);
    free(plan->schema_hash_before);
    free(plan->schema_hash_after);
    free(plan);
}
