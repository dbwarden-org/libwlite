#define _POSIX_C_SOURCE 200809L
/*
 * diff.c — Schema diffing engine
 */

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <ctype.h>
#include "wlite/wlite.h"

/* ── Helpers ─────────────────────────────────────────────────────────── */

static WlDiff *diff_new(size_t capacity) {
    WlDiff *d = calloc(1, sizeof(WlDiff));
    if (d) d->entries = calloc(capacity, sizeof(WlDiffEntry));
    return d;
}

static void diff_add(WlDiff *d, WlDiffOp op, WlSafety safety,
                     const char *table, const char *object, const char *detail) {
    size_t n = d->entry_count;
    d->entries[n].op = op;
    d->entries[n].safety = safety;
    d->entries[n].table = table ? strdup(table) : NULL;
    d->entries[n].object = object ? strdup(object) : NULL;
    d->entries[n].detail = detail ? strdup(detail) : NULL;
    d->entry_count++;
}

static WlTable *find_table(const WlSchema *s, const char *name) {
    for (size_t i = 0; i < s->table_count; i++)
        if (strcmp(s->tables[i].name, name) == 0)
            return &s->tables[i];
    return NULL;
}

static WlColumn *find_column(WlTable *t, const char *name) {
    for (size_t i = 0; i < t->column_count; i++)
        if (strcmp(t->columns[i].name, name) == 0)
            return &t->columns[i];
    return NULL;
}

/* Normalize a default value for comparison.
 * Handles: NULL, boolean (1/0 → TRUE/FALSE), quote stripping, common expressions. */
static char *normalize_default(const char *d) {
    if (!d) return NULL;
    while (*d == ' ' || *d == '\t') d++;
    if (*d == '\0') return NULL;

    /* Strip outer quotes */
    size_t len = strlen(d);
    while (len >= 2 && ((d[0] == '\'' && d[len-1] == '\'') || (d[0] == '"' && d[len-1] == '"'))) {
        d++; len -= 2;
    }

    /* Copy to mutable buffer */
    char *buf = malloc(len + 1);
    memcpy(buf, d, len);
    buf[len] = '\0';

    /* Boolean canonicalization */
    if (strcasecmp(buf, "true") == 0 || strcmp(buf, "1") == 0) { free(buf); return strdup("TRUE"); }
    if (strcasecmp(buf, "false") == 0 || strcmp(buf, "0") == 0) { free(buf); return strdup("FALSE"); }

    /* Timestamp canonicalization */
    char upper[64];
    size_t ulen = len < 63 ? len : 63;
    for (size_t i = 0; i < ulen; i++) upper[i] = toupper((unsigned char)buf[i]);
    upper[ulen] = '\0';
    if (strcmp(upper, "CURRENT_TIMESTAMP") == 0 || strcmp(upper, "NOW()") == 0 ||
        strcmp(upper, "CURRENT_TIMESTAMP()") == 0) {
        free(buf); return strdup("CURRENT_TIMESTAMP");
    }

    return buf;
}

static int str_null_eq(const char *a, const char *b) {
    if (!a && !b) return 1;
    if (!a || !b) return 0;
    return strcmp(a, b) == 0;
}

static int defaults_equal(const char *a, const char *b) {
    char *na = normalize_default(a);
    char *nb = normalize_default(b);
    int eq = str_null_eq(na, nb);
    free(na); free(nb);
    return eq;
}

static WlIndex *find_index(const WlSchema *s, const char *name) {
    for (size_t i = 0; i < s->index_count; i++)
        if (strcmp(s->indexes[i].name, name) == 0)
            return &s->indexes[i];
    return NULL;
}

static WlView *find_view(const WlSchema *s, const char *name) {
    for (size_t i = 0; i < s->view_count; i++)
        if (strcmp(s->views[i].name, name) == 0)
            return &s->views[i];
    return NULL;
}

static WlTrigger *find_trigger(const WlSchema *s, const char *name) {
    for (size_t i = 0; i < s->trigger_count; i++)
        if (strcmp(s->triggers[i].name, name) == 0)
            return &s->triggers[i];
    return NULL;
}

static int cols_equal(WlColumn *a, WlColumn *b) {
    /* Compare type names case-insensitively */
    char ta[64] = {0}, tb[64] = {0};
    if (a->type_name) { size_t n = strlen(a->type_name); if (n > 63) n = 63; for (size_t i = 0; i < n; i++) ta[i] = toupper((unsigned char)a->type_name[i]); }
    if (b->type_name) { size_t n = strlen(b->type_name); if (n > 63) n = 63; for (size_t i = 0; i < n; i++) tb[i] = toupper((unsigned char)b->type_name[i]); }

    return strcmp(a->name, b->name) == 0 &&
           strcmp(ta, tb) == 0 &&
           a->not_null == b->not_null &&
           a->primary_key == b->primary_key &&
           a->is_unique == b->is_unique &&
           a->autoincrement == b->autoincrement &&
           a->is_generated == b->is_generated &&
           a->is_stored == b->is_stored &&
           defaults_equal(a->default_expr, b->default_expr) &&
           str_null_eq(a->collate, b->collate) &&
           str_null_eq(a->generated_expr, b->generated_expr) &&
           str_null_eq(a->fk_table, b->fk_table) &&
           str_null_eq(a->fk_column, b->fk_column);
}

static int fkey_eq(WlForeignKey *a, WlForeignKey *b) {
    if (a->column_count != b->column_count) return 0;
    if (a->ref_column_count != b->ref_column_count) return 0;
    if (!str_null_eq(a->ref_table, b->ref_table)) return 0;
    for (size_t i = 0; i < a->column_count; i++)
        if (strcmp(a->columns[i], b->columns[i]) != 0) return 0;
    for (size_t i = 0; i < a->ref_column_count; i++)
        if (strcmp(a->ref_columns[i], b->ref_columns[i]) != 0) return 0;
    if (a->on_delete != b->on_delete) return 0;
    if (a->on_update != b->on_update) return 0;
    return 1;
}

static int check_eq(WlCheck *a, WlCheck *b) {
    return str_null_eq(a->expression, b->expression);
}

static int unique_eq(WlUnique *a, WlUnique *b) {
    if (a->column_count != b->column_count) return 0;
    for (size_t i = 0; i < a->column_count; i++)
        if (strcmp(a->columns[i], b->columns[i]) != 0) return 0;
    return 1;
}

static int index_eq(WlIndex *a, WlIndex *b) {
    if (a->unique != b->unique) return 0;
    if (a->column_count != b->column_count) return 0;
    for (size_t i = 0; i < a->column_count; i++)
        if (strcmp(a->columns[i], b->columns[i]) != 0) return 0;
    if (!str_null_eq(a->expression, b->expression)) return 0;
    if (!str_null_eq(a->where_clause, b->where_clause)) return 0;
    return 1;
}

static int pk_eq(WlPrimaryKey *a, WlPrimaryKey *b) {
    if (a->column_count != b->column_count) return 0;
    for (size_t i = 0; i < a->column_count; i++)
        if (strcmp(a->columns[i], b->columns[i]) != 0) return 0;
    return 1;
}

/* ── Main diff engine ────────────────────────────────────────────────── */

WlDiff *wl_schema_diff(const WlSchema *current, const WlSchema *desired, wlite_error **error) {
    if (!current || !desired) {
        if (error) {
            *error = calloc(1, sizeof(wlite_error));
            (*error)->code = WLITE_ERR_NULL_PTR;
            (*error)->message = strdup("NULL schema pointer");
        }
        return NULL;
    }

    size_t max_ops = (current->table_count + desired->table_count) * 4 + 128;
    WlDiff *diff = diff_new(max_ops);
    if (!diff) return NULL;

    /* ── Tables: added / dropped / modified ───────────────────────────── */
    for (size_t i = 0; i < desired->table_count; i++) {
        WlTable *dt = &desired->tables[i];
        WlTable *ct = find_table(current, dt->name);
        if (!ct) {
            diff_add(diff, WL_DIFF_ADD_TABLE, WL_SAFETY_SAFE, dt->name, NULL, "new table");
            continue;
        }

        /* Columns added / dropped / modified */
        for (size_t j = 0; j < dt->column_count; j++) {
            WlColumn *dc = &dt->columns[j];
            WlColumn *cc = find_column(ct, dc->name);
            if (!cc) {
                diff_add(diff, WL_DIFF_ADD_COLUMN, WL_SAFETY_SAFE,
                         dt->name, dc->name, "new column");
            } else if (!cols_equal(cc, dc)) {
                /* Check if it's a rebuild-worthy change */
                WlSafety safety = WL_SAFETY_REQUIRES_REBUILD;
                diff_add(diff, WL_DIFF_ALTER_COLUMN, safety,
                         dt->name, dc->name, "column definition changed");
            }
        }
        for (size_t j = 0; j < ct->column_count; j++) {
            WlColumn *cc = &ct->columns[j];
            WlColumn *dc = find_column(dt, cc->name);
            if (!dc) {
                /* Check for exact rename (same definition, different name) */
                int renamed = 0;
                for (size_t k = 0; k < dt->column_count; k++) {
                    WlColumn *maybe_new = &dt->columns[k];
                    if (find_column(ct, maybe_new->name)) continue; /* already exists */
                    if (cols_equal(cc, maybe_new)) {
                        /* Same definition, different name: confirmed rename */
                        diff_add(diff, WL_DIFF_RENAME_COLUMN, WL_SAFETY_SAFE,
                                 dt->name, cc->name, maybe_new->name);
                        renamed = 1;
                        break;
                    }
                }
                if (!renamed) {
                    diff_add(diff, WL_DIFF_DROP_COLUMN, WL_SAFETY_DESTRUCTIVE,
                             dt->name, cc->name, "column removed");
                }
            }
        }

        /* Table options */
        if (ct->strict != dt->strict || ct->without_rowid != dt->without_rowid) {
            diff_add(diff, WL_DIFF_ALTER_TABLE_OPTIONS, WL_SAFETY_REQUIRES_REBUILD,
                     dt->name, NULL, "table options changed");
        }

        /* Primary key */
        if (!pk_eq(&ct->primary_key, &dt->primary_key)) {
            diff_add(diff, WL_DIFF_ALTER_TABLE_OPTIONS, WL_SAFETY_REQUIRES_REBUILD,
                     dt->name, NULL, "primary key changed");
        }

        /* Foreign keys */
        for (size_t j = 0; j < dt->foreign_key_count; j++) {
            int found = 0;
            for (size_t k = 0; k < ct->foreign_key_count; k++) {
                if (fkey_eq(&dt->foreign_keys[j], &ct->foreign_keys[k])) {
                    found = 1; break;
                }
            }
            if (!found) {
                char detail[256];
                snprintf(detail, sizeof(detail), "add FK -> %s", dt->foreign_keys[j].ref_table);
                diff_add(diff, WL_DIFF_ADD_FKEY, WL_SAFETY_REQUIRES_REBUILD,
                         dt->name, NULL, detail);
            }
        }
        for (size_t j = 0; j < ct->foreign_key_count; j++) {
            int found = 0;
            for (size_t k = 0; k < dt->foreign_key_count; k++) {
                if (fkey_eq(&ct->foreign_keys[j], &dt->foreign_keys[k])) {
                    found = 1; break;
                }
            }
            if (!found) {
                char detail[256];
                snprintf(detail, sizeof(detail), "drop FK -> %s", ct->foreign_keys[j].ref_table);
                diff_add(diff, WL_DIFF_DROP_FKEY, WL_SAFETY_REQUIRES_REBUILD,
                         dt->name, NULL, detail);
            }
        }

        /* CHECK constraints */
        for (size_t j = 0; j < dt->check_count; j++) {
            int found = 0;
            for (size_t k = 0; k < ct->check_count; k++) {
                if (check_eq(&dt->checks[j], &ct->checks[k])) { found = 1; break; }
            }
            if (!found) {
                diff_add(diff, WL_DIFF_ADD_CHECK, WL_SAFETY_REQUIRES_REBUILD,
                         dt->name, dt->checks[j].name, "new check constraint");
            }
        }
        for (size_t j = 0; j < ct->check_count; j++) {
            int found = 0;
            for (size_t k = 0; k < dt->check_count; k++) {
                if (check_eq(&ct->checks[j], &dt->checks[k])) { found = 1; break; }
            }
            if (!found) {
                diff_add(diff, WL_DIFF_DROP_CHECK, WL_SAFETY_REQUIRES_REBUILD,
                         dt->name, ct->checks[j].name, "check constraint removed");
            }
        }

        /* UNIQUE constraints */
        for (size_t j = 0; j < dt->unique_count; j++) {
            int found = 0;
            for (size_t k = 0; k < ct->unique_count; k++) {
                if (unique_eq(&dt->uniques[j], &ct->uniques[k])) { found = 1; break; }
            }
            if (!found) {
                diff_add(diff, WL_DIFF_ADD_UNIQUE, WL_SAFETY_REQUIRES_REBUILD,
                         dt->name, dt->uniques[j].name, "new unique constraint");
            }
        }
        for (size_t j = 0; j < ct->unique_count; j++) {
            int found = 0;
            for (size_t k = 0; k < dt->unique_count; k++) {
                if (unique_eq(&ct->uniques[j], &dt->uniques[k])) { found = 1; break; }
            }
            if (!found) {
                diff_add(diff, WL_DIFF_DROP_UNIQUE, WL_SAFETY_REQUIRES_REBUILD,
                         dt->name, ct->uniques[j].name, "unique constraint removed");
            }
        }

        /* Table comment */
        if (!str_null_eq(ct->comment, dt->comment)) {
            diff_add(diff, WL_DIFF_ALTER_TABLE_OPTIONS, WL_SAFETY_SAFE,
                     dt->name, NULL, "comment changed");
        }
    }

    /* Tables in current but not desired → DROP_TABLE */
    for (size_t i = 0; i < current->table_count; i++) {
        WlTable *ct = &current->tables[i];
        if (!find_table(desired, ct->name)) {
            diff_add(diff, WL_DIFF_DROP_TABLE, WL_SAFETY_DESTRUCTIVE,
                     ct->name, NULL, "table removed");
        }
    }

    /* ── Indexes ──────────────────────────────────────────────────────── */
    for (size_t i = 0; i < desired->index_count; i++) {
        WlIndex *di = &desired->indexes[i];
        WlIndex *ci = find_index(current, di->name);
        if (!ci) {
            diff_add(diff, WL_DIFF_ADD_INDEX, WL_SAFETY_SAFE,
                     di->table, di->name, "new index");
        } else if (!index_eq(ci, di)) {
            diff_add(diff, WL_DIFF_ALTER_INDEX, WL_SAFETY_SAFE,
                     di->table, di->name, "index definition changed");
        }
    }
    for (size_t i = 0; i < current->index_count; i++) {
        WlIndex *ci = &current->indexes[i];
        if (!find_index(desired, ci->name)) {
            diff_add(diff, WL_DIFF_DROP_INDEX, WL_SAFETY_SAFE,
                     ci->table, ci->name, "index removed");
        }
    }

    /* ── Views ────────────────────────────────────────────────────────── */
    for (size_t i = 0; i < desired->view_count; i++) {
        WlView *dv = &desired->views[i];
        WlView *cv = find_view(current, dv->name);
        if (!cv) {
            diff_add(diff, WL_DIFF_ADD_TABLE, WL_SAFETY_SAFE,
                     dv->name, NULL, "new view");
        } else if (!str_null_eq(cv->sql, dv->sql)) {
            diff_add(diff, WL_DIFF_ALTER_VIEW, WL_SAFETY_SAFE,
                     dv->name, NULL, "view definition changed");
        }
    }
    for (size_t i = 0; i < current->view_count; i++) {
        WlView *cv = &current->views[i];
        if (!find_view(desired, cv->name)) {
            diff_add(diff, WL_DIFF_DROP_TABLE, WL_SAFETY_SAFE,
                     cv->name, NULL, "view removed");
        }
    }

    /* ── Triggers ─────────────────────────────────────────────────────── */
    for (size_t i = 0; i < desired->trigger_count; i++) {
        WlTrigger *dt = &desired->triggers[i];
        WlTrigger *ct = find_trigger(current, dt->name);
        if (!ct) {
            diff_add(diff, WL_DIFF_ADD_TABLE, WL_SAFETY_SAFE,
                     dt->name, NULL, "new trigger");
        } else if (!str_null_eq(ct->sql, dt->sql)) {
            diff_add(diff, WL_DIFF_ALTER_TRIGGER, WL_SAFETY_SAFE,
                     dt->name, NULL, "trigger definition changed");
        }
    }
    for (size_t i = 0; i < current->trigger_count; i++) {
        WlTrigger *ct = &current->triggers[i];
        if (!find_trigger(desired, ct->name)) {
            diff_add(diff, WL_DIFF_DROP_TABLE, WL_SAFETY_SAFE,
                     ct->name, NULL, "trigger removed");
        }
    }

    return diff;
}

void wl_diff_free(WlDiff *diff) {
    if (!diff) return;
    for (size_t i = 0; i < diff->entry_count; i++) {
        free(diff->entries[i].table);
        free(diff->entries[i].object);
        free(diff->entries[i].detail);
    }
    free(diff->entries);
    free(diff);
}
