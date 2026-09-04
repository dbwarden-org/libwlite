/*
 * introspect.c — Introspect a live SQLite database into WlSchema
 */

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <sqlite3.h>
#include "wlite/wlite.h"
#include "internal.h"

/* ── Version comparison ──────────────────────────────────────────────── */

static int version_at_least(const char *lib, int major, int minor, int patch) {
    int lmajor = 0, lminor = 0, lpatch = 0;
    sscanf(lib, "%d.%d.%d", &lmajor, &lminor, &lpatch);
    if (lmajor != major) return lmajor > major;
    if (lminor != minor) return lminor > minor;
    return lpatch >= patch;
}

void wl_sqlite_capabilities(sqlite3 *db, wlite_sqlite_caps *caps) {
    if (!caps) return;
    memset(caps, 0, sizeof(*caps));
    if (!db) return;

    const char *ver = sqlite3_libversion();
    caps->has_without_rowid = 1; /* 3.8.0+ */
    caps->has_generated = version_at_least(ver, 3, 31, 0);
    caps->has_rename_column = version_at_least(ver, 3, 25, 0);
    caps->has_drop_column = version_at_least(ver, 3, 35, 0);
    caps->has_strict = version_at_least(ver, 3, 37, 0);
}

/* ── Parse CREATE TABLE SQL for extras ───────────────────────────────── */

static void parse_create_table_extras(const char *sql, WlTable *table) {
    if (!sql) return;
    /* Check for STRICT, WITHOUT ROWID */
    size_t len = strlen(sql);
    char *upper = malloc(len + 1);
    for (size_t i = 0; i < len; i++) upper[i] = toupper((unsigned char)sql[i]);
    upper[len] = '\0';
    if (strstr(upper, "STRICT")) table->strict = 1;
    if (strstr(upper, "WITHOUT ROWID")) table->without_rowid = 1;
    free(upper);
}

/* ── Introspect a single table ───────────────────────────────────────── */

static int introspect_table(sqlite3 *db, const char *table_name,
                            const char *create_sql, WlTable *table) {
    table->name = strdup(table_name);
    parse_create_table_extras(create_sql, table);

    /* ── Columns via PRAGMA table_xinfo (includes generated columns) ── */
    sqlite3_stmt *stmt;
    char pragma[512];
    snprintf(pragma, sizeof(pragma), "PRAGMA table_xinfo(\"%s\")", table_name);
    int rc = sqlite3_prepare_v2(db, pragma, -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            WlColumn col = {0};
            col.name = strdup((const char *)sqlite3_column_text(stmt, 1));
            col.type_name = strdup((const char *)sqlite3_column_text(stmt, 2));
            col.affinity = resolve_affinity(col.type_name);
            col.not_null = sqlite3_column_int(stmt, 3);
            col.default_expr = strdup(sqlite3_column_text(stmt, 4)
                ? (const char *)sqlite3_column_text(stmt, 4) : "");
            col.primary_key = sqlite3_column_int(stmt, 5);
            /* column 6 = pk (1=single PK, part of composite PK) */
            /* column 7 = hidden */
            const char *is_generated = (const char *)sqlite3_column_text(stmt, 8);
            /* column 9 = generation expression */
            const char *gen_expr = (const char *)sqlite3_column_text(stmt, 9);

            if (is_generated && strcmp(is_generated, "computed") == 0) {
                col.is_generated = 1;
                col.is_stored = 1; /* SQLite generated columns are always STORED */
                if (gen_expr) col.generated_expr = strdup(gen_expr);
            }

            table->columns = realloc(table->columns,
                (table->column_count + 1) * sizeof(WlColumn));
            table->columns[table->column_count++] = col;
        }
        sqlite3_finalize(stmt);
    }

    /* ── Composite primary key via PRAGMA index_list ─────────────────── */
    rc = sqlite3_prepare_v2(db,
        "PRAGMA index_list", -1, &stmt, NULL);
    /* We need to query per-table, but index_list is not table-scoped.
       Instead, check sqlite_master for implicit PK. */
    {
        sqlite3_stmt *pk_stmt;
        char pk_pragma[512];
        snprintf(pk_pragma, sizeof(pk_pragma),
            "SELECT name FROM sqlite_master WHERE type='index' "
            "AND tbl_name='%s' AND sql LIKE '%%PRIMARY KEY%%'", table_name);
        rc = sqlite3_prepare_v2(db, pk_pragma, -1, &pk_stmt, NULL);
        if (rc == SQLITE_OK && sqlite3_step(pk_stmt) == SQLITE_ROW) {
            /* composite PK exists — get columns */
            const char *idx_name = (const char *)sqlite3_column_text(pk_stmt, 0);
            sqlite3_stmt *info_stmt;
            char info_pragma[512];
            snprintf(info_pragma, sizeof(info_pragma),
                "PRAGMA index_info(\"%s\")", idx_name);
            rc = sqlite3_prepare_v2(db, info_pragma, -1, &info_stmt, NULL);
            if (rc == SQLITE_OK) {
                while (sqlite3_step(info_stmt) == SQLITE_ROW) {
                    const char *col = (const char *)sqlite3_column_text(info_stmt, 2);
                    if (col) {
                        table->primary_key.columns = realloc(table->primary_key.columns,
                            (table->primary_key.column_count + 1) * sizeof(char *));
                        table->primary_key.columns[table->primary_key.column_count++] = strdup(col);
                    }
                }
                sqlite3_finalize(info_stmt);
            }
        }
        sqlite3_finalize(pk_stmt);
    }

    /* ── Foreign keys ────────────────────────────────────────────────── */
    {
        sqlite3_stmt *fk_stmt;
        char fk_pragma[512];
        snprintf(fk_pragma, sizeof(fk_pragma),
            "PRAGMA foreign_key_list(\"%s\")", table_name);
        rc = sqlite3_prepare_v2(db, fk_pragma, -1, &fk_stmt, NULL);
        if (rc == SQLITE_OK) {
            int last_seq = -1;
            WlForeignKey *current_fk = NULL;
            while (sqlite3_step(fk_stmt) == SQLITE_ROW) {
                int seq = sqlite3_column_int(fk_stmt, 0);
                const char *ref_table = (const char *)sqlite3_column_text(fk_stmt, 2);
                const char *from_col = (const char *)sqlite3_column_text(fk_stmt, 3);
                const char *to_col = (const char *)sqlite3_column_text(fk_stmt, 4);
                const char *on_update = (const char *)sqlite3_column_text(fk_stmt, 5);
                const char *on_delete = (const char *)sqlite3_column_text(fk_stmt, 6);

                if (seq != last_seq) {
                    table->foreign_keys = realloc(table->foreign_keys,
                        (table->foreign_key_count + 1) * sizeof(WlForeignKey));
                    current_fk = &table->foreign_keys[table->foreign_key_count++];
                    memset(current_fk, 0, sizeof(*current_fk));
                    current_fk->ref_table = strdup(ref_table);
                    if (on_update) {
                        if (strstr(on_update, "CASCADE")) current_fk->on_update = WL_FK_CASCADE;
                        else if (strstr(on_update, "RESTRICT")) current_fk->on_update = WL_FK_RESTRICT;
                        else if (strstr(on_update, "SET NULL")) current_fk->on_update = WL_FK_SET_NULL;
                        else if (strstr(on_update, "SET DEFAULT")) current_fk->on_update = WL_FK_SET_DEFAULT;
                    }
                    if (on_delete) {
                        if (strstr(on_delete, "CASCADE")) current_fk->on_delete = WL_FK_CASCADE;
                        else if (strstr(on_delete, "RESTRICT")) current_fk->on_delete = WL_FK_RESTRICT;
                        else if (strstr(on_delete, "SET NULL")) current_fk->on_delete = WL_FK_SET_NULL;
                        else if (strstr(on_delete, "SET DEFAULT")) current_fk->on_delete = WL_FK_SET_DEFAULT;
                    }
                    last_seq = seq;
                }
                if (current_fk) {
                    current_fk->columns = realloc(current_fk->columns,
                        (current_fk->column_count + 1) * sizeof(char *));
                    current_fk->columns[current_fk->column_count++] = strdup(from_col);
                    current_fk->ref_columns = realloc(current_fk->ref_columns,
                        (current_fk->ref_column_count + 1) * sizeof(char *));
                    current_fk->ref_columns[current_fk->ref_column_count++] = strdup(to_col);
                }
            }
            sqlite3_finalize(fk_stmt);
        }
    }

    return SQLITE_OK;
}

/* ── Introspect indexes ──────────────────────────────────────────────── */

static int introspect_indexes(sqlite3 *db, WlSchema *schema) {
    for (size_t t = 0; t < schema->table_count; t++) {
        const char *table_name = schema->tables[t].name;
        sqlite3_stmt *stmt;
        char pragma[256];
        snprintf(pragma, sizeof(pragma), "PRAGMA index_list(\"%s\")", table_name);
        int rc = sqlite3_prepare_v2(db, pragma, -1, &stmt, NULL);
        if (rc != SQLITE_OK) continue;

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            WlIndex idx = {0};
            int unique = sqlite3_column_int(stmt, 1);
            const char *name = (const char *)sqlite3_column_text(stmt, 2);
            if (!name) continue;

            /* Skip implicit indexes (names that are just numbers) */
            int is_implicit = 1;
            for (const char *p = name; *p; p++) {
                if (*p < '0' || *p > '9') { is_implicit = 0; break; }
            }
            if (is_implicit) continue;

            idx.name = strdup(name);
            idx.table = strdup(table_name);
            idx.unique = unique;

            sqlite3_stmt *info_stmt;
            char info_pragma[256];
            snprintf(info_pragma, sizeof(info_pragma), "PRAGMA index_info(\"%s\")", name);
            rc = sqlite3_prepare_v2(db, info_pragma, -1, &info_stmt, NULL);
            if (rc == SQLITE_OK) {
                while (sqlite3_step(info_stmt) == SQLITE_ROW) {
                    const char *col = (const char *)sqlite3_column_text(info_stmt, 2);
                    if (col) {
                        idx.columns = realloc(idx.columns, (idx.column_count + 1) * sizeof(char *));
                        idx.columns[idx.column_count++] = strdup(col);
                    }
                }
                sqlite3_finalize(info_stmt);
            }

            schema->indexes = realloc(schema->indexes,
                (schema->index_count + 1) * sizeof(WlIndex));
            schema->indexes[schema->index_count++] = idx;
        }
        sqlite3_finalize(stmt);
    }
    return SQLITE_OK;
}

/* ── Introspect views ────────────────────────────────────────────────── */

static int introspect_views(sqlite3 *db, WlSchema *schema) {
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db,
        "SELECT name, sql FROM sqlite_master WHERE type='view' ORDER BY name",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return rc;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        WlView view = {0};
        view.name = strdup((const char *)sqlite3_column_text(stmt, 0));
        view.sql = strdup((const char *)sqlite3_column_text(stmt, 1));
        schema->views = realloc(schema->views,
            (schema->view_count + 1) * sizeof(WlView));
        schema->views[schema->view_count++] = view;
    }
    sqlite3_finalize(stmt);
    return SQLITE_OK;
}

/* ── Introspect triggers ─────────────────────────────────────────────── */

static int introspect_triggers(sqlite3 *db, WlSchema *schema) {
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db,
        "SELECT name, tbl_name, sql FROM sqlite_master "
        "WHERE type='trigger' ORDER BY name",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) return rc;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        WlTrigger trig = {0};
        trig.name = strdup((const char *)sqlite3_column_text(stmt, 0));
        trig.table = strdup((const char *)sqlite3_column_text(stmt, 1));
        trig.sql = strdup((const char *)sqlite3_column_text(stmt, 2));
        schema->triggers = realloc(schema->triggers,
            (schema->trigger_count + 1) * sizeof(WlTrigger));
        schema->triggers[schema->trigger_count++] = trig;
    }
    sqlite3_finalize(stmt);
    return SQLITE_OK;
}

/* ── Main introspection entry point ──────────────────────────────────── */

WlSchema *wl_schema_introspect(sqlite3 *db, wlite_error **error) {
    if (!db) {
        if (error) {
            *error = calloc(1, sizeof(wlite_error));
            (*error)->code = WLITE_INVALID_ARGUMENT;
            (*error)->message = strdup("NULL database pointer");
        }
        return NULL;
    }

    WlSchema *schema = calloc(1, sizeof(WlSchema));
    if (!schema) return NULL;
    schema->version = 1;

    /* Get all tables */
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db,
        "SELECT name, sql FROM sqlite_master "
        "WHERE type='table' AND name NOT LIKE 'sqlite_%' "
        "ORDER BY name", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        if (error) {
            *error = calloc(1, sizeof(wlite_error));
            (*error)->code = WLITE_SQLITE_ERROR;
            (*error)->message = strdup(sqlite3_errmsg(db));
            (*error)->sqlite_code = rc;
        }
        free(schema);
        return NULL;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(stmt, 0);
        const char *sql = (const char *)sqlite3_column_text(stmt, 1);

        schema->tables = realloc(schema->tables,
            (schema->table_count + 1) * sizeof(WlTable));
        memset(&schema->tables[schema->table_count], 0, sizeof(WlTable));

        introspect_table(db, name, sql, &schema->tables[schema->table_count]);
        schema->table_count++;
    }
    sqlite3_finalize(stmt);

    /* Get indexes */
    introspect_indexes(db, schema);

    /* Mark columns as UNIQUE from single-column UNIQUE indexes
       and from implicit UNIQUE constraints (detected via sqlite_master.sql) */
    for (size_t i = 0; i < schema->index_count; i++) {
        WlIndex *idx = &schema->indexes[i];
        if (idx->unique && idx->column_count == 1) {
            for (size_t j = 0; j < schema->table_count; j++) {
                if (strcmp(schema->tables[j].name, idx->table) == 0) {
                    for (size_t k = 0; k < schema->tables[j].column_count; k++) {
                        if (strcmp(schema->tables[j].columns[k].name, idx->columns[0]) == 0) {
                            schema->tables[j].columns[k].is_unique = 1;
                        }
                    }
                }
            }
        }
    }
    /* Also detect implicit UNIQUE from CREATE TABLE SQL */
    for (size_t t = 0; t < schema->table_count; t++) {
        sqlite3_stmt *stmt;
        char pragma[256];
        snprintf(pragma, sizeof(pragma),
            "SELECT sql FROM sqlite_master WHERE type='table' AND name='%s'", schema->tables[t].name);
        if (sqlite3_prepare_v2(db, pragma, -1, &stmt, NULL) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                const char *sql = (const char *)sqlite3_column_text(stmt, 0);
                if (sql) {
                    /* Check for column-level UNIQUE constraints in the CREATE TABLE SQL */
                    /* Pattern: column_name TYPE ... UNIQUE ... */
                    for (size_t c = 0; c < schema->tables[t].column_count; c++) {
                        char pattern[256];
                        snprintf(pattern, sizeof(pattern), "%s ", schema->tables[t].columns[c].name);
                        const char *pos = strstr(sql, pattern);
                        if (pos) {
                            /* Check if UNIQUE appears before the next comma or closing paren */
                            const char *check = pos + strlen(pattern);
                            const char *comma = strchr(check, ',');
                            const char *paren = strchr(check, ')');
                            const char *end = comma ? comma : paren;
                            if (end) {
                                char *region = malloc(end - check + 1);
                                memcpy(region, check, end - check);
                                region[end - check] = '\0';
                                /* Convert to uppercase for comparison */
                                for (size_t i = 0; i < (size_t)(end - check); i++)
                                    region[i] = toupper((unsigned char)region[i]);
                                if (strstr(region, "UNIQUE"))
                                    schema->tables[t].columns[c].is_unique = 1;
                                free(region);
                            }
                        }
                    }
                }
            }
            sqlite3_finalize(stmt);
        }
    }

    /* Get views */
    introspect_views(db, schema);

    /* Get triggers */
    introspect_triggers(db, schema);

    return schema;
}
