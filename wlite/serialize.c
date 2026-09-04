#define _POSIX_C_SOURCE 200809L
/*
 * serialize.c — JSON and DSL serialization of WlSchema
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "wlite/wlite.h"

/* ── Helpers ─────────────────────────────────────────────────────────── */

static void wn(wlite_writer *w, const char *s) { w->write(w, s, strlen(s)); w->write(w, "\n", 1); }
static void wni(wlite_writer *w, int indent, const char *s) {
    for (int i = 0; i < indent; i++) w->write(w, "  ", 2);
    wn(w, s);
}

/* ── JSON ────────────────────────────────────────────────────────────── */

static void json_kv(wlite_writer *w, int indent, const char *k, const char *v, int comma) {
    char buf[1024];
    snprintf(buf, sizeof(buf), "%*s\"%s\": \"%s\"%s",
        indent * 2, "", k, v ? v : "", comma ? "," : "");
    wn(w, buf);
}

static void json_ki(wlite_writer *w, int indent, const char *k, int v, int comma) {
    char buf[256];
    snprintf(buf, sizeof(buf), "%*s\"%s\": %d%s", indent * 2, "", k, v, comma ? "," : "");
    wn(w, buf);
}

static void json_kb(wlite_writer *w, int indent, const char *k, int v, int comma) {
    char buf[256];
    snprintf(buf, sizeof(buf), "%*s\"%s\": %s%s", indent * 2, "", k, v ? "true" : "false", comma ? "," : "");
    wn(w, buf);
}

static void json_write_column(wlite_writer *w, int indent, WlColumn *c, int last) {
    wni(w, indent, "{");
    json_kv(w, indent+1, "name", c->name, 1);
    json_kv(w, indent+1, "type", c->type_name, 1);
    json_kb(w, indent+1, "not_null", c->not_null, 1);
    json_kb(w, indent+1, "primary_key", c->primary_key, 1);
    json_kb(w, indent+1, "autoincrement", c->autoincrement, 1);
    json_kb(w, indent+1, "unique", c->is_unique, 1);
    if (c->default_expr) json_kv(w, indent+1, "default", c->default_expr, 1);
    if (c->collate) json_kv(w, indent+1, "collate", c->collate, 1);
    if (c->is_generated) {
        json_kb(w, indent+1, "generated", 1, 1);
        json_kb(w, indent+1, "stored", c->is_stored, 1);
        if (c->generated_expr) json_kv(w, indent+1, "generated_expr", c->generated_expr, 1);
    }
    if (c->fk_table) {
        json_kv(w, indent+1, "references", c->fk_table, 0);
        /* hack: append column */
        char buf[256];
        snprintf(buf, sizeof(buf), "    \"references_column\": \"%s\",", c->fk_column ? c->fk_column : "");
        wn(w, buf);
    }
    char end[64];
    snprintf(end, sizeof(end), "%s", last ? "" : ",");
    /* fix: just output } directly */
    wni(w, indent, last ? "}" : "},");
}

static void json_write_table(wlite_writer *w, int indent, WlTable *t, int last) {
    char buf[256];
    snprintf(buf, sizeof(buf), "%*s{", indent * 2, "");
    wn(w, buf);
    json_kv(w, indent+1, "name", t->name, 1);
    json_kb(w, indent+1, "strict", t->strict, 1);
    json_kb(w, indent+1, "without_rowid", t->without_rowid, 0);

    /* columns */
    wni(w, indent+1, "\"columns\": [");
    for (size_t i = 0; i < t->column_count; i++)
        json_write_column(w, indent+2, &t->columns[i], i == t->column_count - 1);
    wni(w, indent+1, "]");

    /* foreign keys */
    if (t->foreign_key_count > 0) {
        wni(w, indent+1, "\"foreign_keys\": [");
        for (size_t i = 0; i < t->foreign_key_count; i++) {
            WlForeignKey *fk = &t->foreign_keys[i];
            char fkbuf[512];
            snprintf(fkbuf, sizeof(fkbuf),
                "%*s{\"ref_table\": \"%s\", \"columns\": [",
                (indent+2) * 2, "", fk->ref_table);
            wn(w, fkbuf);
            for (size_t j = 0; j < fk->column_count; j++) {
                char cb[128];
                snprintf(cb, sizeof(cb), "%*s\"%s\"%s",
                    (indent+3) * 2, "", fk->columns[j],
                    j < fk->column_count - 1 ? "," : "");
                wn(w, cb);
            }
            char end[128];
            snprintf(end, sizeof(end), "%*s]%s",
                (indent+2) * 2, "", i < t->foreign_key_count - 1 ? "}," : "}");
            wn(w, end);
        }
        wni(w, indent+1, "]");
    }

    /* checks */
    if (t->check_count > 0) {
        wni(w, indent+1, "\"checks\": [");
        for (size_t i = 0; i < t->check_count; i++) {
            char ckbuf[512];
            snprintf(ckbuf, sizeof(ckbuf), "%*s{\"expression\": \"%s\"}%s",
                (indent+2) * 2, "", t->checks[i].expression ? t->checks[i].expression : "",
                i < t->check_count - 1 ? "," : "");
            wn(w, ckbuf);
        }
        wni(w, indent+1, "]");
    }

    /* uniques */
    if (t->unique_count > 0) {
        wni(w, indent+1, "\"uniques\": [");
        for (size_t i = 0; i < t->unique_count; i++) {
            char uqbuf[512];
            snprintf(uqbuf, sizeof(uqbuf), "%*s{\"columns\": [",
                (indent+2) * 2, "");
            wn(w, uqbuf);
            for (size_t j = 0; j < t->uniques[i].column_count; j++) {
                char cb[128];
                snprintf(cb, sizeof(cb), "%*s\"%s\"%s",
                    (indent+3) * 2, "", t->uniques[i].columns[j],
                    j < t->uniques[i].column_count - 1 ? "," : "");
                wn(w, cb);
            }
            char end[128];
            snprintf(end, sizeof(end), "%*s]}%s",
                (indent+2) * 2, "", i < t->unique_count - 1 ? "," : "");
            wn(w, end);
        }
        wni(w, indent+1, "]");
    }

    snprintf(buf, sizeof(buf), "%s", last ? "" : ",");
    wni(w, indent, last ? "}" : "},");
}

int wl_schema_write_json(const WlSchema *schema, wlite_writer *w, wlite_error **error) {
    (void)error;
    if (!schema || !w) return WLITE_ERR_NULL_PTR;

    wni(w, 0, "{");
    json_ki(w, 1, "version", schema->version, 1);

    /* tables */
    wni(w, 1, "\"tables\": [");
    for (size_t i = 0; i < schema->table_count; i++)
        json_write_table(w, 2, &schema->tables[i], i == schema->table_count - 1);
    wni(w, 1, "],");

    /* indexes */
    wni(w, 1, "\"indexes\": [");
    for (size_t i = 0; i < schema->index_count; i++) {
        WlIndex *idx = &schema->indexes[i];
        char buf[512];
        snprintf(buf, sizeof(buf), "%*s{\"name\": \"%s\", \"table\": \"%s\", \"unique\": %s}",
            4, "", idx->name, idx->table, idx->unique ? "true" : "false");
        char comma[8];
        snprintf(comma, sizeof(comma), "%s", i < schema->index_count - 1 ? "," : "");
        strcat(buf, comma);
        wn(w, buf);
    }
    wni(w, 1, "],");

    /* views */
    wni(w, 1, "\"views\": [");
    for (size_t i = 0; i < schema->view_count; i++) {
        WlView *v = &schema->views[i];
        char buf[512];
        snprintf(buf, sizeof(buf), "%*s{\"name\": \"%s\"}%s",
            4, "", v->name, i < schema->view_count - 1 ? "," : "");
        wn(w, buf);
    }
    wni(w, 1, "]");

    wn(w, "}");
    return WLITE_OK;
}

/* ── DSL ─────────────────────────────────────────────────────────────── */

static void dsl_write_column(wlite_writer *w, int indent, WlColumn *c) {
    char buf[1024];
    snprintf(buf, sizeof(buf), "%*s%s %s", indent * 2, "", c->name, c->type_name);
    wn(w, buf);
    if (c->not_null) wni(w, indent, "    not null");
    if (c->primary_key && !c->autoincrement) wni(w, indent, "    primary key");
    if (c->primary_key && c->autoincrement) wni(w, indent, "    primary key autoincrement");
    if (c->is_unique) wni(w, indent, "    unique");
    if (c->default_expr && c->default_expr[0]) {
        char defbuf[512];
        snprintf(defbuf, sizeof(defbuf), "%*sdefault %s", indent * 2 + 4, "", c->default_expr);
        wn(w, defbuf);
    }
    if (c->collate) {
        char cbuf[256];
        snprintf(cbuf, sizeof(cbuf), "%*scollate %s", indent * 2 + 4, "", c->collate);
        wn(w, cbuf);
    }
    if (c->fk_table) {
        char fkbuf[512];
        snprintf(fkbuf, sizeof(fkbuf), "%*sreferences %s(%s)",
            indent * 2 + 4, "", c->fk_table, c->fk_column ? c->fk_column : "");
        wn(w, fkbuf);
        if (c->fk_on_delete) {
            char delbuf[256];
            snprintf(delbuf, sizeof(delbuf), "%*son delete %s", indent * 2 + 4, "", c->fk_on_delete);
            wn(w, delbuf);
        }
    }
    if (c->is_generated && c->generated_expr) {
        char gbuf[1024];
        snprintf(gbuf, sizeof(gbuf), "%*sgenerated (%s) %s",
            indent * 2 + 4, "", c->generated_expr, c->is_stored ? "stored" : "virtual");
        wn(w, gbuf);
    }
}

int wl_schema_write_dsl(const WlSchema *schema, wlite_writer *w, wlite_error **error) {
    (void)error;
    if (!schema || !w) return WLITE_ERR_NULL_PTR;

    wn(w, "# wlite schema");
    wn(w, "");

    for (size_t i = 0; i < schema->table_count; i++) {
        WlTable *t = &schema->tables[i];
        char buf[256];
        snprintf(buf, sizeof(buf), "table %s {", t->name);
        wn(w, buf);
        if (t->strict) wni(w, 1, "strict");
        if (t->without_rowid) wni(w, 1, "without rowid");
        if (t->comment) {
            char cmt[512];
            snprintf(cmt, sizeof(cmt), "comment \"%s\"", t->comment);
            wni(w, 1, cmt);
        }
        wn(w, "");
        for (size_t j = 0; j < t->column_count; j++)
            dsl_write_column(w, 1, &t->columns[j]);
        if (t->primary_key.column_count > 1) {
            char pk[512];
            snprintf(pk, sizeof(pk), "    primary key (");
            for (size_t j = 0; j < t->primary_key.column_count; j++) {
                char *tmp;
                if (j > 0) {
                    tmp = malloc(strlen(pk) + strlen(t->primary_key.columns[j]) + 3);
                    sprintf(tmp, "%s, %s", pk, t->primary_key.columns[j]);
                } else {
                    tmp = malloc(strlen(pk) + strlen(t->primary_key.columns[j]) + 1);
                    sprintf(tmp, "%s%s", pk, t->primary_key.columns[j]);
                }
                strcpy(pk, tmp); free(tmp);
            }
            strcat(pk, ")");
            wni(w, 1, pk);
        }
        for (size_t j = 0; j < t->check_count; j++) {
            char ck[512];
            if (t->checks[j].name)
                snprintf(ck, sizeof(ck), "    constraint %s check (%s)",
                    t->checks[j].name, t->checks[j].expression);
            else
                snprintf(ck, sizeof(ck), "    check (%s)", t->checks[j].expression);
            wni(w, 1, ck);
        }
        for (size_t j = 0; j < t->unique_count; j++) {
            char uq[512];
            snprintf(uq, sizeof(uq), "    unique (");
            for (size_t k = 0; k < t->uniques[j].column_count; k++) {
                if (k > 0) strcat(uq, ", ");
                strcat(uq, t->uniques[j].columns[k]);
            }
            strcat(uq, ")");
            wni(w, 1, uq);
        }
        for (size_t j = 0; j < t->foreign_key_count; j++) {
            WlForeignKey *fk = &t->foreign_keys[j];
            char fkbuf[512];
            snprintf(fkbuf, sizeof(fkbuf), "    foreign key (");
            for (size_t k = 0; k < fk->column_count; k++) {
                if (k > 0) strcat(fkbuf, ", ");
                strcat(fkbuf, fk->columns[k]);
            }
            strcat(fkbuf, ") references ");
            strcat(fkbuf, fk->ref_table);
            strcat(fkbuf, "(");
            for (size_t k = 0; k < fk->ref_column_count; k++) {
                if (k > 0) strcat(fkbuf, ", ");
                strcat(fkbuf, fk->ref_columns[k]);
            }
            strcat(fkbuf, ")");
            if (fk->on_delete == WL_FK_CASCADE) strcat(fkbuf, " on delete cascade");
            if (fk->on_update == WL_FK_CASCADE) strcat(fkbuf, " on update cascade");
            wni(w, 1, fkbuf);
        }
        wn(w, "}");
        wn(w, "");
    }

    for (size_t i = 0; i < schema->index_count; i++) {
        WlIndex *idx = &schema->indexes[i];
        char buf[512];
        snprintf(buf, sizeof(buf), "index %s {", idx->name);
        wn(w, buf);
        if (idx->unique) wni(w, 1, "unique");
        char onbuf[512];
        snprintf(onbuf, sizeof(onbuf), "    on %s(", idx->table);
        for (size_t j = 0; j < idx->column_count; j++) {
            if (j > 0) strcat(onbuf, ", ");
            strcat(onbuf, idx->columns[j]);
        }
        if (idx->expression) {
            strcat(onbuf, idx->expression);
        }
        strcat(onbuf, ")");
        wn(w, onbuf);
        if (idx->where_clause) {
            char whbuf[512];
            snprintf(whbuf, sizeof(whbuf), "    where %s", idx->where_clause);
            wn(w, whbuf);
        }
        wn(w, "}");
        wn(w, "");
    }

    for (size_t i = 0; i < schema->view_count; i++) {
        WlView *v = &schema->views[i];
        char buf[256];
        snprintf(buf, sizeof(buf), "view %s {", v->name);
        wn(w, buf);
        if (v->sql) {
            char sqlbuf[2048];
            snprintf(sqlbuf, sizeof(sqlbuf), "    select %s", v->sql);
            wn(w, sqlbuf);
        }
        wn(w, "}");
        wn(w, "");
    }

    for (size_t i = 0; i < schema->trigger_count; i++) {
        WlTrigger *tr = &schema->triggers[i];
        char buf[256];
        snprintf(buf, sizeof(buf), "trigger %s {", tr->name);
        wn(w, buf);
        if (tr->table) {
            char tbuf[256];
            snprintf(tbuf, sizeof(tbuf), "    on %s", tr->table);
            wn(w, tbuf);
        }
        if (tr->sql) {
            char sbuf[2048];
            snprintf(sbuf, sizeof(sbuf), "    %s", tr->sql);
            wn(w, sbuf);
        }
        wn(w, "}");
        wn(w, "");
    }

    return WLITE_OK;
}
