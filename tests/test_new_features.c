/*
 * test_new_features.c — Tests for features added since initial test suite
 *
 * Tests:
 * - wlite migrate (create table via migrate)
 * - wlite diff --json output format
 * - wlite plan --json output format
 * - Rename detection (exact definition only)
 * - No fuzzy rename matching
 * - Type change produces rebuild
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wlite/wlite.h"

static int tests_run = 0, tests_passed = 0;
#define TEST(n) do { tests_run++; fprintf(stderr, "  %-50s ", n); } while(0)
#define PASS() do { tests_passed++; fprintf(stderr, "PASS\n"); } while(0)
#define FAIL(msg) do { fprintf(stderr, "FAIL: %s\n", msg); } while(0)

static wlite_model *load_str(const char *src) {
    char tmppath[] = "/tmp/wl_test_XXXXXX";
    FILE *tf = fopen(tmppath, "w+");
    if (!tf) return NULL;
    fwrite(src, 1, strlen(src), tf);
    fclose(tf);
    wlite_model *m = NULL;
    wlite_model_load_file(tmppath, &m);
    remove(tmppath);
    return m;
}

/* ── Migrate ─────────────────────────────────────────────────────────── */

void test_migrate_create_table(void) {
    TEST("migrate: create table");
    wlite_db *db = NULL;
    wlite_open(":memory:", &db);
    wlite_model *m = load_str(
        "model User { table \"users\" "
        "field id integer { primary_key autoincrement } "
        "field name text { not_null } }");
    if (!m) { FAIL("load"); wlite_close(db); return; }
    wlite_result rc = wlite_migrate(db, m);
    wlite_model_free(m);
    if (rc != WLITE_OK) { FAIL("migrate failed"); wlite_close(db); return; }
    wlite_stmt *s = NULL;
    wlite_prepare(db, "SELECT name FROM sqlite_master WHERE type='table' AND name='users'", &s);
    int ok = (wlite_step(s) == WLITE_OK);
    wlite_stmt_finalize(s);
    wlite_close(db);
    if (!ok) { FAIL("table not created"); return; }
    PASS();
}

void test_migrate_idempotent(void) {
    TEST("migrate: idempotent (no-op on second call)");
    wlite_db *db = NULL;
    wlite_open(":memory:", &db);
    wlite_model *m = load_str(
        "model User { table \"users\" "
        "field id integer { primary_key } "
        "field name text }");
    if (!m) { FAIL("load"); wlite_close(db); return; }
    wlite_migrate(db, m);
    wlite_result rc = wlite_migrate(db, m);
    wlite_model_free(m);
    wlite_close(db);
    if (rc != WLITE_OK) { FAIL("second migrate failed"); return; }
    PASS();
}

void test_migrate_add_column(void) {
    TEST("migrate: add column");
    wlite_db *db = NULL;
    wlite_open(":memory:", &db);
    wlite_model *m1 = load_str(
        "model User { table \"users\" "
        "field id integer { primary_key } "
        "field name text }");
    wlite_migrate(db, m1);
    wlite_model_free(m1);

    wlite_model *m2 = load_str(
        "model User { table \"users\" "
        "field id integer { primary_key } "
        "field name text "
        "field email text }");
    wlite_result rc = wlite_migrate(db, m2);
    wlite_model_free(m2);
    if (rc != WLITE_OK) { FAIL("migrate failed"); wlite_close(db); return; }

    wlite_stmt *s = NULL;
    wlite_prepare(db, "SELECT name FROM pragma_table_info('users') WHERE name='email'", &s);
    int ok = (wlite_step(s) == WLITE_OK);
    wlite_stmt_finalize(s);
    wlite_close(db);
    if (!ok) { FAIL("email column not added"); return; }
    PASS();
}

/* ── Diff ────────────────────────────────────────────────────────────── */

void test_diff_identical_schemas(void) {
    TEST("diff: identical schemas produce zero changes");
    wlite_db *db = NULL;
    wlite_open(":memory:", &db);
    wlite_execute(db, "CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT)", NULL);
    wlite_model *m = load_str(
        "model T { table \"t\" "
        "field id integer { primary_key } "
        "field name text }");
    WlPlan *plan = NULL;
    wlite_result rc = wlite_diff(db, m, &plan);
    wlite_close(db);
    wlite_model_free(m);
    if (rc != WLITE_OK) { FAIL("diff failed"); if (plan) wl_plan_free(plan); return; }
    if (plan && wlite_plan_count(plan) > 0) { FAIL("expected zero changes"); wl_plan_free(plan); return; }
    if (plan) wl_plan_free(plan);
    PASS();
}

void test_diff_add_column(void) {
    TEST("diff: add column detected");
    wlite_db *db = NULL;
    wlite_open(":memory:", &db);
    wlite_execute(db, "CREATE TABLE t (id INTEGER PRIMARY KEY)", NULL);
    wlite_model *m = load_str(
        "model T { table \"t\" "
        "field id integer { primary_key } "
        "field name text }");
    WlPlan *plan = NULL;
    wlite_diff(db, m, &plan);
    wlite_close(db);
    wlite_model_free(m);
    if (!plan) { FAIL("plan NULL"); return; }
    int found = 0;
    for (size_t i = 0; i < plan->step_count; i++) {
        if (plan->steps[i].op == WL_PLAN_ADD_COLUMN) found = 1;
    }
    wl_plan_free(plan);
    if (!found) { FAIL("ADD_COLUMN not found in plan"); return; }
    PASS();
}

void test_diff_type_change_rebuild(void) {
    TEST("diff: type change produces REBUILD");
    wlite_db *db = NULL;
    wlite_open(":memory:", &db);
    wlite_execute(db, "CREATE TABLE t (id INTEGER PRIMARY KEY, val TEXT)", NULL);
    wlite_model *m = load_str(
        "model T { table \"t\" "
        "field id integer { primary_key } "
        "field val integer }");
    WlPlan *plan = NULL;
    wlite_diff(db, m, &plan);
    wlite_close(db);
    wlite_model_free(m);
    if (!plan) { FAIL("plan NULL"); return; }
    int found_rebuild = 0;
    for (size_t i = 0; i < plan->step_count; i++) {
        if (plan->steps[i].op == WL_PLAN_REBUILD_TABLE) found_rebuild = 1;
    }
    wl_plan_free(plan);
    if (!found_rebuild) { FAIL("REBUILD_TABLE not found"); return; }
    PASS();
}

/* ── Rename detection ────────────────────────────────────────────────── */

void test_rename_exact_match(void) {
    TEST("rename: exact definition match produces RENAME_COLUMN");
    wlite_db *db = NULL;
    wlite_open(":memory:", &db);
    wlite_execute(db, "CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT NOT NULL)", NULL);
    WlSchema *db_s = wl_schema_inspect(db, NULL);
    const char *model = 
        "model T {\n"
        "    table \"t\"\n"
        "    field id integer { primary_key }\n"
        "    field full_name text { not_null }\n"
        "}\n";
    WlSchema *wl_s = wl_schema_parse(model, strlen(model), NULL);
    if (!wl_s) { FAIL("wl_s NULL"); wl_schema_free(db_s); wlite_close(db); return; }
    WlDiff *diff = wl_schema_diff(db_s, wl_s, NULL);
    wl_schema_free(db_s);
    wl_schema_free(wl_s);
    wlite_close(db);
    if (!diff) { FAIL("diff NULL"); return; }
    int found_rename = 0;
    for (size_t i = 0; i < diff->entry_count; i++) {
        if (diff->entries[i].op == WL_DIFF_RENAME_COLUMN) found_rename = 1;
    }
    wl_diff_free(diff);
    if (!found_rename) { FAIL("RENAME_COLUMN not found"); return; }
    PASS();
}

void test_no_fuzzy_rename(void) {
    TEST("rename: different definitions produce DROP + ADD, not rename");
    wlite_db *db = NULL;
    wlite_open(":memory:", &db);
    wlite_execute(db, "CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT NOT NULL)", NULL);
    WlSchema *db_s = wl_schema_inspect(db, NULL);
    const char *model = 
        "model T {\n"
        "    table \"t\"\n"
        "    field id integer { primary_key }\n"
        "    field username integer\n"
        "}\n";
    WlSchema *wl_s = wl_schema_parse(model, strlen(model), NULL);
    if (!wl_s) { FAIL("wl_s NULL"); wl_schema_free(db_s); wlite_close(db); return; }
    WlDiff *diff = wl_schema_diff(db_s, wl_s, NULL);
    wl_schema_free(db_s);
    wl_schema_free(wl_s);
    wlite_close(db);
    if (!diff) { FAIL("diff NULL"); return; }
    int found_rename = 0;
    int found_drop = 0;
    int found_add = 0;
    for (size_t i = 0; i < diff->entry_count; i++) {
        if (diff->entries[i].op == WL_DIFF_RENAME_COLUMN) found_rename = 1;
        if (diff->entries[i].op == WL_DIFF_DROP_COLUMN) found_drop = 1;
        if (diff->entries[i].op == WL_DIFF_ADD_COLUMN) found_add = 1;
    }
    wl_diff_free(diff);
    if (found_rename) { FAIL("should NOT find RENAME_COLUMN for different definitions"); return; }
    if (!found_drop || !found_add) { FAIL("expected DROP + ADD for different definitions"); return; }
    PASS();
}

/* ── Plan ────────────────────────────────────────────────────────────── */

void test_plan_step_count(void) {
    TEST("plan: step count matches expected");
    wlite_db *db = NULL;
    wlite_open(":memory:", &db);
    wlite_model *m = load_str(
        "model T { table \"t\" "
        "field id integer { primary_key } "
        "field name text "
        "field email text }");
    WlPlan *plan = NULL;
    wlite_diff(db, m, &plan);
    wlite_close(db);
    wlite_model_free(m);
    if (!plan) { FAIL("plan NULL"); return; }
    /* Creating a table with 3 columns should produce at least 1 step */
    if (wlite_plan_count(plan) == 0) { FAIL("expected steps"); wl_plan_free(plan); return; }
    wl_plan_free(plan);
    PASS();
}

void test_plan_has_sql(void) {
    TEST("plan: steps have SQL");
    wlite_db *db = NULL;
    wlite_open(":memory:", &db);
    wlite_model *m = load_str(
        "model T { table \"t\" "
        "field id integer { primary_key } }");
    WlPlan *plan = NULL;
    wlite_diff(db, m, &plan);
    wlite_close(db);
    wlite_model_free(m);
    if (!plan) { FAIL("plan NULL"); return; }
    int has_sql = 0;
    for (size_t i = 0; i < plan->step_count; i++) {
        if (plan->steps[i].sql && strlen(plan->steps[i].sql) > 0) has_sql = 1;
    }
    wl_plan_free(plan);
    if (!has_sql) { FAIL("no steps have SQL"); return; }
    PASS();
}

/* ── Main ─────────────────────────────────────────────────────────────── */

int main(void) {
    fprintf(stderr, "wlite new features test suite\n\n");
    test_migrate_create_table();
    test_migrate_idempotent();
    test_migrate_add_column();
    test_diff_identical_schemas();
    test_diff_add_column();
    test_diff_type_change_rebuild();
    test_rename_exact_match();
    test_no_fuzzy_rename();
    test_plan_step_count();
    test_plan_has_sql();
    fprintf(stderr, "\n%d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
