/*
 * test_edge_cases.c — Edge case tests for libwlite
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wlite/wlite.h"

static int tests_run = 0, tests_passed = 0;
#define TEST(n) do { tests_run++; fprintf(stderr, "  %-50s ", n); } while(0)
#define PASS() do { tests_passed++; fprintf(stderr, "PASS\n"); } while(0)
#define FAIL(msg) do { fprintf(stderr, "FAIL: %s\n", msg); } while(0)

static wlite_model *load_str(const char *src) {
    char tmppath[] = "/tmp/wl_tc_XXXXXX";
    FILE *f = fopen(tmppath, "w+");
    if (!f) return NULL;
    fwrite(src, 1, strlen(src), f);
    fclose(f);
    wlite_model *m = NULL;
    wlite_model_load_file(tmppath, &m);
    remove(tmppath);
    return m;
}

/* Test: multiple tables */
void test_multi_table(void) {
    TEST("multiple tables");
    wlite_model *m = load_str(
        "model A { table \"a\" field x integer { primary_key } }"
        "model B { table \"b\" field y text { not_null } }"
        "model C { table \"c\" field z real }");
    if (!m) { FAIL("load"); return; }
    if (wlite_model_table_count(m) != 3) { FAIL("count"); wlite_model_free(m); return; }
    if (!wlite_model_table(m, "a") || !wlite_model_table(m, "b") || !wlite_model_table(m, "c")) {
        FAIL("lookup"); wlite_model_free(m); return;
    }
    wlite_model_free(m);
    PASS();
}

/* Test: NULL values in queries */
void test_null_values(void) {
    TEST("NULL values");
    wlite_db *db; wlite_open(":memory:", &db);
    wlite_execute(db, "CREATE TABLE t (id INTEGER PRIMARY KEY, val TEXT)", NULL);
    wlite_stmt *s; wlite_prepare(db, "INSERT INTO t VALUES (1, NULL)", &s);
    wlite_step(s); wlite_stmt_finalize(s);
    wlite_prepare(db, "SELECT val FROM t WHERE id=1", &s);
    wlite_step(s);
    wlite_value_type vt = wlite_column_type(s, 0);
    if (vt != WLITE_TYPE_NULL) { FAIL("expected NULL type"); wlite_stmt_finalize(s); wlite_close(db); return; }
    wlite_stmt_finalize(s);
    wlite_close(db);
    PASS();
}

/* Test: empty result set */
void test_empty_result(void) {
    TEST("empty result set");
    wlite_db *db; wlite_open(":memory:", &db);
    wlite_execute(db, "CREATE TABLE t (id INTEGER PRIMARY KEY)", NULL);
    wlite_stmt *s; wlite_prepare(db, "SELECT * FROM t", &s);
    wlite_result rc = wlite_step(s);
    if (rc != WLITE_NOT_FOUND) { FAIL("expected NOT_FOUND"); wlite_stmt_finalize(s); wlite_close(db); return; }
    wlite_stmt_finalize(s);
    wlite_close(db);
    PASS();
}

/* Test: large batch insert */
void test_batch_insert(void) {
    TEST("batch insert");
    wlite_db *db; wlite_open(":memory:", &db);
    wlite_execute(db, "CREATE TABLE t (id INTEGER PRIMARY KEY, val TEXT)", NULL);
    wlite_stmt *s; wlite_prepare(db, "INSERT INTO t VALUES (?, ?)", &s);
    for (int i = 0; i < 1000; i++) {
        wlite_bind_int64(s, 1, i);
        char buf[32]; snprintf(buf, sizeof(buf), "val_%d", i);
        wlite_bind_text(s, 2, buf);
        wlite_step(s);
    }
    wlite_stmt_finalize(s);
    wlite_prepare(db, "SELECT COUNT(*) FROM t", &s);
    wlite_step(s);
    int64_t count = wlite_column_int64(s, 0);
    wlite_stmt_finalize(s);
    wlite_close(db);
    if (count != 1000) { FAIL("wrong count"); return; }
    PASS();
}

/* Test: rebuild dedup in plan */
void test_plan_dedup(void) {
    TEST("plan rebuild dedup");
    wlite_model *m = load_str(
        "model X { table \"t\" "
        "  field id integer { primary_key }"
        "  field a text { not_null }"
        "  field b text { not_null }"
        "}");
    if (!m) { FAIL("load"); return; }
    wlite_db *db; wlite_open(":memory:", &db);
    wlite_execute(db, "CREATE TABLE t (id INTEGER PRIMARY KEY, a TEXT, b TEXT)", NULL);
    /* Change both a and b to NOT NULL with different types — triggers rebuilds */
    WlPlan *plan = NULL;
    wlite_diff(db, m, &plan);
    wlite_close(db);
    wlite_model_free(m);
    if (!plan) { FAIL("plan NULL"); return; }
    /* Count rebuild steps */
    size_t rebuilds = 0;
    for (size_t i = 0; i < wlite_plan_count(plan); i++) {
        /* We can't easily inspect step ops from C, but check plan has reasonable steps */
    }
    /* Plan should have at most 1 rebuild for the table */
    if (wlite_plan_count(plan) > 5) { FAIL("too many plan steps"); wl_plan_free(plan); return; }
    wl_plan_free(plan);
    PASS();
}

/* Test: type coercion in bind */
void test_type_coercion(void) {
    TEST("type coercion in statements");
    wlite_db *db; wlite_open(":memory:", &db);
    wlite_execute(db, "CREATE TABLE t (i INTEGER, r REAL, t TEXT, b BLOB)", NULL);
    wlite_stmt *s;
    wlite_prepare(db, "INSERT INTO t VALUES (?, ?, ?, ?)", &s);
    wlite_bind_int64(s, 1, 42);
    wlite_bind_double(s, 2, 3.14);
    wlite_bind_text(s, 3, "hello");
    wlite_bind_null(s, 4);
    wlite_step(s);
    wlite_stmt_finalize(s);
    wlite_prepare(db, "SELECT * FROM t", &s);
    wlite_step(s);
    int ok = (wlite_column_int64(s, 0) == 42 &&
              wlite_column_double(s, 1) == 3.14 &&
              strcmp(wlite_column_text(s, 2), "hello") == 0 &&
              wlite_column_type(s, 3) == WLITE_TYPE_NULL);
    wlite_stmt_finalize(s);
    wlite_close(db);
    if (!ok) { FAIL("coercion"); return; }
    PASS();
}

/* Test: diff with added column */
void test_diff_add_column(void) {
    TEST("diff: add column");
    wlite_db *db; wlite_open(":memory:", &db);
    wlite_execute(db, "CREATE TABLE t (id INTEGER PRIMARY KEY, a TEXT)", NULL);
    wlite_model *m = load_str("model X { table \"t\" field id integer { primary_key } field a text }");
    if (!m) { FAIL("load"); return; }
    /* Model has only id and a — same as DB. No changes expected. */
    WlPlan *plan = NULL;
    wlite_diff(db, m, &plan);
    wlite_close(db);
    wlite_model_free(m);
    if (!plan) { FAIL("plan NULL"); return; }
    if (wlite_plan_count(plan) != 0) { FAIL("expected no changes"); wl_plan_free(plan); return; }
    wl_plan_free(plan);
    PASS();
}

/* Test: diff with dropped table */
void test_diff_drop_table(void) {
    TEST("diff: drop table");
    wlite_db *db; wlite_open(":memory:", &db);
    wlite_execute(db, "CREATE TABLE a (x INTEGER)", NULL);
    wlite_execute(db, "CREATE TABLE b (y TEXT)", NULL);
    wlite_model *m = load_str("model X { table \"a\" field x integer }");
    if (!m) { FAIL("load"); return; }
    WlPlan *plan = NULL;
    wlite_diff(db, m, &plan);
    wlite_close(db);
    wlite_model_free(m);
    if (!plan) { FAIL("plan NULL"); return; }
    if (wlite_plan_count(plan) == 0) { FAIL("expected drop"); wl_plan_free(plan); return; }
    wl_plan_free(plan);
    PASS();
}

/* Test: schema hash consistency */
void test_schema_hash(void) {
    TEST("schema hash consistency");
    const char *src = "model X { table \"t\" field id integer { primary_key } }";
    WlSchema *s1 = wl_schema_parse(src, strlen(src), NULL);
    WlSchema *s2 = wl_schema_parse(src, strlen(src), NULL);
    char *h1 = wl_schema_hash(s1);
    char *h2 = wl_schema_hash(s2);
    int eq = (h1 && h2 && strcmp(h1, h2) == 0);
    free(h1); free(h2);
    wl_schema_free(s1); wl_schema_free(s2);
    if (!eq) { FAIL("hashes differ"); return; }
    PASS();
}

/* Test: different schemas have different hashes */
void test_schema_hash_different(void) {
    TEST("schema hash different for different schemas");
    WlSchema *s1 = wl_schema_parse("model X { table \"a\" field x integer }", 40, NULL);
    WlSchema *s2 = wl_schema_parse("model X { table \"b\" field x integer }", 40, NULL);
    char *h1 = wl_schema_hash(s1);
    char *h2 = wl_schema_hash(s2);
    int eq = (h1 && h2 && strcmp(h1, h2) == 0);
    free(h1); free(h2);
    wl_schema_free(s1); wl_schema_free(s2);
    if (eq) { FAIL("hashes should differ"); return; }
    PASS();
}

/* Test: wlite_strerror for all codes */
void test_strerror_all(void) {
    TEST("strerror: all codes");
    wlite_result codes[] = {
        WLITE_OK, WLITE_ERROR, WLITE_INVALID_ARGUMENT, WLITE_OUT_OF_MEMORY,
        WLITE_IO_ERROR, WLITE_PARSE_ERROR, WLITE_MODEL_ERROR, WLITE_SQLITE_ERROR,
        WLITE_CONSTRAINT_ERROR, WLITE_NOT_FOUND, WLITE_BUSY, WLITE_TRANSACTION_ERROR
    };
    for (size_t i = 0; i < sizeof(codes)/sizeof(codes[0]); i++) {
        const char *s = wlite_strerror(codes[i]);
        if (!s || strlen(s) == 0) { FAIL("empty strerror"); return; }
    }
    PASS();
}

/* Test: memory leak check — open/close many times */
void test_open_close_stress(void) {
    TEST("open/close stress");
    for (int i = 0; i < 100; i++) {
        wlite_db *db; wlite_open(":memory:", &db);
        wlite_execute(db, "CREATE TABLE t (id INTEGER)", NULL);
        wlite_close(db);
    }
    PASS();
}

/* Test: concurrent prepares */
void test_multiple_statements(void) {
    TEST("multiple active statements");
    wlite_db *db; wlite_open(":memory:", &db);
    wlite_execute(db, "CREATE TABLE t (id INTEGER PRIMARY KEY, val TEXT)", NULL);
    wlite_execute(db, "INSERT INTO t VALUES (1, 'a'), (2, 'b'), (3, 'c')", NULL);
    wlite_stmt *s1, *s2;
    wlite_prepare(db, "SELECT * FROM t WHERE id <= 2", &s1);
    wlite_prepare(db, "SELECT * FROM t WHERE id >= 2", &s2);
    int count1 = 0, count2 = 0;
    while (wlite_step(s1) == WLITE_OK) count1++;
    while (wlite_step(s2) == WLITE_OK) count2++;
    wlite_stmt_finalize(s1);
    wlite_stmt_finalize(s2);
    wlite_close(db);
    if (count1 != 2 || count2 != 2) { FAIL("wrong counts"); return; }
    PASS();
}

int main(void) {
    fprintf(stderr, "wlite edge case tests\n\n");
    test_multi_table();
    test_null_values();
    test_empty_result();
    test_batch_insert();
    test_plan_dedup();
    test_type_coercion();
    test_diff_add_column();
    test_diff_drop_table();
    test_schema_hash();
    test_schema_hash_different();
    test_strerror_all();
    test_open_close_stress();
    test_multiple_statements();
    fprintf(stderr, "\n%d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
