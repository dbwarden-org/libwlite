/*
 * compile.c — .wlitem compiled model format
 *
 * Binary format:
 *   Header: "WLIT" magic (4B) + version (4B) + model_name (len-prefixed) + model_version (4B) + table_count (4B)
 *   Per table: name (len-prefixed) + flags (1B) + comment (len-prefixed) + column_count (4B) + columns... + pk_count (4B) + pks... + fk_count (4B) + fks... + check_count (4B) + checks... + unique_count (4B) + uniques...
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wlite/wlite.h"
#include "internal.h"

#define WLITEM_MAGIC 0x54494C57  /* "WLIT" */
#define WLITEM_VERSION 1

/* ── Write helpers ────────────────────────────────────────────────────── */

static int write_u8(FILE *f, uint8_t v) { return fwrite(&v, 1, 1, f) == 1 ? 0 : -1; }
static int write_u32(FILE *f, uint32_t v) { return fwrite(&v, 4, 1, f) == 1 ? 0 : -1; }
static int write_i32(FILE *f, int32_t v) { return fwrite(&v, 4, 1, f) == 1 ? 0 : -1; }

static int write_str(FILE *f, const char *s) {
    uint32_t len = s ? (uint32_t)strlen(s) : 0;
    if (write_u32(f, len) < 0) return -1;
    if (len > 0 && fwrite(s, 1, len, f) != len) return -1;
    return 0;
}

/* ── Read helpers ─────────────────────────────────────────────────────── */

static int read_u8(FILE *f, uint8_t *v) { return fread(v, 1, 1, f) == 1 ? 0 : -1; }
static int read_u32(FILE *f, uint32_t *v) { return fread(v, 4, 1, f) == 1 ? 0 : -1; }
static int read_i32(FILE *f, int32_t *v) { return fread(v, 4, 1, f) == 1 ? 0 : -1; }

static char *read_str(FILE *f) {
    uint32_t len;
    if (read_u32(f, &len) < 0) return NULL;
    if (len == 0) return NULL;
    char *s = malloc(len + 1);
    if (!s) return NULL;
    if (fread(s, 1, len, f) != len) { free(s); return NULL; }
    s[len] = '\0';
    return s;
}

/* ── Compile: WlSchema → .wlitem ─────────────────────────────────────── */

int wl_model_compile(const WlSchema *schema, const char *path) {
    if (!schema || !path) return WLITE_INVALID_ARGUMENT;
    FILE *f = fopen(path, "wb");
    if (!f) return WLITE_IO_ERROR;

    /* Header */
    write_u32(f, WLITEM_MAGIC);
    write_u32(f, WLITEM_VERSION);
    write_str(f, schema->model_name);
    write_i32(f, schema->model_version);
    write_u32(f, (uint32_t)schema->table_count);

    for (size_t i = 0; i < schema->table_count; i++) {
        WlTable *t = &schema->tables[i];
        write_str(f, t->name);
        uint8_t flags = (t->strict ? 1 : 0) | (t->without_rowid ? 2 : 0);
        write_u8(f, flags);
        write_str(f, t->comment);

        /* Columns */
        write_u32(f, (uint32_t)t->column_count);
        for (size_t j = 0; j < t->column_count; j++) {
            WlColumn *c = &t->columns[j];
            write_str(f, c->name);
            write_str(f, c->type_name);
            uint8_t cflags = (c->not_null ? 1 : 0) | (c->primary_key ? 2 : 0) |
                             (c->is_unique ? 4 : 0) | (c->autoincrement ? 8 : 0) |
                             (c->is_generated ? 16 : 0) | (c->is_stored ? 32 : 0);
            write_u8(f, cflags);
            write_str(f, c->default_expr);
            write_str(f, c->collate);
            write_str(f, c->generated_expr);
            write_str(f, c->fk_table);
            write_str(f, c->fk_column);
        }

        /* Primary key */
        write_u32(f, (uint32_t)t->primary_key.column_count);
        for (size_t j = 0; j < t->primary_key.column_count; j++)
            write_str(f, t->primary_key.columns[j]);

        /* Foreign keys */
        write_u32(f, (uint32_t)t->foreign_key_count);
        for (size_t j = 0; j < t->foreign_key_count; j++) {
            WlForeignKey *fk = &t->foreign_keys[j];
            write_u32(f, (uint32_t)fk->column_count);
            for (size_t k = 0; k < fk->column_count; k++) write_str(f, fk->columns[k]);
            write_str(f, fk->ref_table);
            write_u32(f, (uint32_t)fk->ref_column_count);
            for (size_t k = 0; k < fk->ref_column_count; k++) write_str(f, fk->ref_columns[k]);
            write_u8(f, (uint8_t)fk->on_delete);
            write_u8(f, (uint8_t)fk->on_update);
        }

        /* Checks */
        write_u32(f, (uint32_t)t->check_count);
        for (size_t j = 0; j < t->check_count; j++) {
            write_str(f, t->checks[j].name);
            write_str(f, t->checks[j].expression);
        }

        /* Uniques */
        write_u32(f, (uint32_t)t->unique_count);
        for (size_t j = 0; j < t->unique_count; j++) {
            write_str(f, t->uniques[j].name);
            write_u32(f, (uint32_t)t->uniques[j].column_count);
            for (size_t k = 0; k < t->uniques[j].column_count; k++)
                write_str(f, t->uniques[j].columns[k]);
        }
    }

    fclose(f);
    return WLITE_OK;
}

/* ── Load compiled: .wlitem → WlSchema ───────────────────────────────── */

static WlSchema *load_compiled(FILE *f) {
    uint32_t magic, version;
    if (read_u32(f, &magic) < 0 || magic != WLITEM_MAGIC) return NULL;
    if (read_u32(f, &version) < 0 || version > WLITEM_VERSION) return NULL;

    WlSchema *schema = calloc(1, sizeof(WlSchema));
    if (!schema) return NULL;
    schema->version = 1;

    schema->model_name = read_str(f);
    read_i32(f, &schema->model_version);
    read_u32(f, (uint32_t *)&schema->table_count);

    schema->tables = calloc(schema->table_count, sizeof(WlTable));
    for (size_t i = 0; i < schema->table_count; i++) {
        WlTable *t = &schema->tables[i];
        t->name = read_str(f);
        uint8_t flags; read_u8(f, &flags);
        t->strict = flags & 1;
        t->without_rowid = flags & 2;
        t->comment = read_str(f);

        /* Columns */
        read_u32(f, (uint32_t *)&t->column_count);
        t->columns = calloc(t->column_count, sizeof(WlColumn));
        for (size_t j = 0; j < t->column_count; j++) {
            WlColumn *c = &t->columns[j];
            c->name = read_str(f);
            c->type_name = read_str(f);
            c->affinity = resolve_affinity(c->type_name);
            uint8_t cflags; read_u8(f, &cflags);
            c->not_null = (cflags & 1) != 0;
            c->primary_key = (cflags & 2) != 0;
            c->is_unique = (cflags & 4) != 0;
            c->autoincrement = (cflags & 8) != 0;
            c->is_generated = (cflags & 16) != 0;
            c->is_stored = (cflags & 32) != 0;
            c->default_expr = read_str(f);
            c->collate = read_str(f);
            c->generated_expr = read_str(f);
            c->fk_table = read_str(f);
            c->fk_column = read_str(f);
            /* Clean up empty strings */
            if (c->default_expr && !c->default_expr[0]) { free(c->default_expr); c->default_expr = NULL; }
            if (c->collate && !c->collate[0]) { free(c->collate); c->collate = NULL; }
            if (c->generated_expr && !c->generated_expr[0]) { free(c->generated_expr); c->generated_expr = NULL; }
            if (c->fk_table && !c->fk_table[0]) { free(c->fk_table); c->fk_table = NULL; }
            if (c->fk_column && !c->fk_column[0]) { free(c->fk_column); c->fk_column = NULL; }
        }

        /* Primary key */
        read_u32(f, (uint32_t *)&t->primary_key.column_count);
        t->primary_key.columns = calloc(t->primary_key.column_count, sizeof(char *));
        for (size_t j = 0; j < t->primary_key.column_count; j++)
            t->primary_key.columns[j] = read_str(f);

        /* Foreign keys */
        read_u32(f, (uint32_t *)&t->foreign_key_count);
        t->foreign_keys = calloc(t->foreign_key_count, sizeof(WlForeignKey));
        for (size_t j = 0; j < t->foreign_key_count; j++) {
            WlForeignKey *fk = &t->foreign_keys[j];
            read_u32(f, (uint32_t *)&fk->column_count);
            fk->columns = calloc(fk->column_count, sizeof(char *));
            for (size_t k = 0; k < fk->column_count; k++) fk->columns[k] = read_str(f);
            fk->ref_table = read_str(f);
            read_u32(f, (uint32_t *)&fk->ref_column_count);
            fk->ref_columns = calloc(fk->ref_column_count, sizeof(char *));
            for (size_t k = 0; k < fk->ref_column_count; k++) fk->ref_columns[k] = read_str(f);
            uint8_t on_del, on_upd;
            read_u8(f, &on_del); read_u8(f, &on_upd);
            fk->on_delete = (wlite_fk_action)on_del;
            fk->on_update = (wlite_fk_action)on_upd;
        }

        /* Checks */
        read_u32(f, (uint32_t *)&t->check_count);
        t->checks = calloc(t->check_count, sizeof(WlCheck));
        for (size_t j = 0; j < t->check_count; j++) {
            t->checks[j].name = read_str(f);
            t->checks[j].expression = read_str(f);
        }

        /* Uniques */
        read_u32(f, (uint32_t *)&t->unique_count);
        t->uniques = calloc(t->unique_count, sizeof(WlUnique));
        for (size_t j = 0; j < t->unique_count; j++) {
            t->uniques[j].name = read_str(f);
            read_u32(f, (uint32_t *)&t->uniques[j].column_count);
            t->uniques[j].columns = calloc(t->uniques[j].column_count, sizeof(char *));
            for (size_t k = 0; k < t->uniques[j].column_count; k++)
                t->uniques[j].columns[k] = read_str(f);
        }
    }

    return schema;
}

WlSchema *wl_model_load_compiled_raw(const void *data, size_t size) {
    if (!data || size < 12) return NULL;
    /* Write to temp file and load */
    char tmppath[] = "/tmp/wlitem_XXXXXX";
    FILE *f = fopen(tmppath, "w+b");
    if (!f) return NULL;
    fwrite(data, 1, size, f);
    rewind(f);
    WlSchema *schema = load_compiled(f);
    fclose(f);
    remove(tmppath);
    return schema;
}
