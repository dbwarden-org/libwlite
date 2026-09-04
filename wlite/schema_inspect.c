/*
 * schema_inspect.c — Inspect live SQLite database schema
 */

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include "wlite/wlite.h"
#include "internal.h"

/* Reuse WlSchema as the schema inspection type */
WlSchema *wl_schema_inspect(wlite_db *db, wlite_error **error) {
    if (!db || !db->sqlite) {
        if (error) { *error = calloc(1, sizeof(wlite_error)); (*error)->code = WLITE_ERR_NULL_PTR;
            (*error)->message = strdup("NULL database"); }
        return NULL;
    }
    return wl_schema_introspect(db->sqlite, error);
}
