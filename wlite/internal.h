/*
 * internal.h — Internal shared declarations for libwlite
 */

#ifndef WLITE_INTERNAL_H
#define WLITE_INTERNAL_H

#include "wlite/wlite.h"
#include <sqlite3.h>

/* Shared type affinity resolver */
wlite_col_type resolve_affinity(const char *type);

/* Database struct (shared between schema.c and migrate.c) */
struct wlite_db {
    sqlite3 *sqlite;
};

/* Model struct (shared between schema.c and migrate.c) */
struct wlite_model {
    WlSchema *schema;
};

#endif /* WLITE_INTERNAL_H */
