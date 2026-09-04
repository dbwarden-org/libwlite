/*
 * record.c — Generic record (row) access
 */

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include "wlite/wlite.h"

struct wlite_record {
    int column_count;
    char **names;
    wlite_value_type *types;
    int64_t *ints;
    double *doubles;
    char **texts;
    size_t *blob_sizes;
    void **blobs;
};

static wlite_record *record_from_stmt(wlite_stmt *stmt) {
    int n = wlite_column_count(stmt);
    wlite_record *r = calloc(1, sizeof(wlite_record));
    if (!r) return NULL;
    r->column_count = n;
    r->names = calloc(n, sizeof(char *));
    r->types = calloc(n, sizeof(wlite_value_type));
    r->ints = calloc(n, sizeof(int64_t));
    r->doubles = calloc(n, sizeof(double));
    r->texts = calloc(n, sizeof(char *));
    r->blob_sizes = calloc(n, sizeof(size_t));
    r->blobs = calloc(n, sizeof(void *));

    for (int i = 0; i < n; i++) {
        const char *name = wlite_column_name(stmt, i);
        r->names[i] = name ? strdup(name) : NULL;
        r->types[i] = wlite_column_type(stmt, i);
        switch (r->types[i]) {
            case WLITE_TYPE_INTEGER: r->ints[i] = wlite_column_int64(stmt, i); break;
            case WLITE_TYPE_REAL:    r->doubles[i] = wlite_column_double(stmt, i); break;
            case WLITE_TYPE_TEXT: {
                const char *t = wlite_column_text(stmt, i);
                r->texts[i] = t ? strdup(t) : NULL;
                break;
            }
            case WLITE_TYPE_BLOB: {
                size_t sz = wlite_column_bytes(stmt, i);
                const void *b = wlite_column_blob(stmt, i);
                if (b && sz > 0) {
                    r->blobs[i] = malloc(sz);
                    memcpy(r->blobs[i], b, sz);
                }
                r->blob_sizes[i] = sz;
                break;
            }
            default: break;
        }
    }
    return r;
}

wlite_record *wlite_record_from_stmt(wlite_stmt *stmt) {
    return record_from_stmt(stmt);
}

void wlite_record_free(wlite_record *record) {
    if (!record) return;
    for (int i = 0; i < record->column_count; i++) {
        free(record->names[i]);
        free(record->texts[i]);
        free(record->blobs[i]);
    }
    free(record->names);
    free(record->types);
    free(record->ints);
    free(record->doubles);
    free(record->texts);
    free(record->blob_sizes);
    free(record->blobs);
    free(record);
}

int wlite_record_column_count(const wlite_record *record) {
    return record ? record->column_count : 0;
}

const char *wlite_record_column_name(const wlite_record *record, int index) {
    if (!record || index < 0 || index >= record->column_count) return NULL;
    return record->names[index];
}

wlite_value_type wlite_record_column_type(const wlite_record *record, int index) {
    if (!record || index < 0 || index >= record->column_count) return WLITE_TYPE_NULL;
    return record->types[index];
}

int wlite_record_find(const wlite_record *record, const char *name) {
    if (!record || !name) return -1;
    for (int i = 0; i < record->column_count; i++)
        if (record->names[i] && strcmp(record->names[i], name) == 0) return i;
    return -1;
}

int64_t wlite_record_int64(const wlite_record *record, int index) {
    if (!record || index < 0 || index >= record->column_count) return 0;
    return record->ints[index];
}

double wlite_record_double(const wlite_record *record, int index) {
    if (!record || index < 0 || index >= record->column_count) return 0.0;
    return record->doubles[index];
}

const char *wlite_record_text(const wlite_record *record, int index) {
    if (!record || index < 0 || index >= record->column_count) return NULL;
    return record->texts[index];
}

const void *wlite_record_blob(const wlite_record *record, int index) {
    if (!record || index < 0 || index >= record->column_count) return NULL;
    return record->blobs[index];
}

size_t wlite_record_blob_bytes(const wlite_record *record, int index) {
    if (!record || index < 0 || index >= record->column_count) return 0;
    return record->blob_sizes[index];
}
