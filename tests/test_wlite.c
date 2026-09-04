#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wlite/wlite.h"

static int tests_run = 0, tests_passed = 0;
#define TEST(n) do { tests_run++; fprintf(stderr, "  %-40s ", n); } while(0)
#define PASS() do { tests_passed++; fprintf(stderr, "PASS\n"); } while(0)
#define FAIL(msg) do { fprintf(stderr, "FAIL: %s\n", msg); } while(0)

static wlite_model *load_str(const char *src) {
    char path[] = "/tmp/wlite_test_XXXXXX";
    FILE *f = tmpfile();
    if (!f) return NULL;
    fwrite(src, 1, strlen(src), f);
    fflush(f);
    rewind(f);
    /* Read back into a temp file */
    char tmppath[] = "/tmp/wl_XXXXXX";
    FILE *tf = fopen(tmppath, "w+");
    if (!tf) { fclose(f); return NULL; }
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) fwrite(buf, 1, n, tf);
    fclose(f);
    fclose(tf);
    wlite_model *m = NULL;
    wlite_model_load_file(tmppath, &m);
    remove(tmppath);
    return m;
}

void test_parse_basic(void) {
    TEST("parse: basic model");
    wlite_model *m = load_str("model User { table \"users\" field id integer { primary_key } field name text { not_null } }");
    if (!m) { FAIL("load returned NULL"); return; }
    if (wlite_model_table_count(m) != 1) { FAIL("expected 1 table"); wlite_model_free(m); return; }
    const wlite_table *t = wlite_model_table(m, "users");
    if (!t) { FAIL("table not found"); wlite_model_free(m); return; }
    if (wlite_table_field_count(t) != 2) { FAIL("expected 2 fields"); wlite_model_free(m); return; }
    wlite_model_free(m);
    PASS();
}

void test_parse_strict(void) {
    TEST("parse: strict table");
    wlite_model *m = load_str("model X { table \"t\" strict field id integer { primary_key } }");
    if (!m) { FAIL("load failed"); return; }
    wlite_model_free(m);
    PASS();
}

void test_validate(void) {
    TEST("validate: valid model");
    wlite_model *m = load_str("model X { table \"t\" field id integer { primary_key } }");
    if (!m) { FAIL("load failed"); return; }
    wlite_result rc = wlite_model_validate(m);
    wlite_model_free(m);
    if (rc != WLITE_OK) { FAIL("validate should succeed"); return; }
    PASS();
}

void test_table_lookup(void) {
    TEST("table lookup by name");
    wlite_model *m = load_str("model User { table \"users\" field id integer }");
    if (!m) { FAIL("load failed"); return; }
    const wlite_table *t = wlite_model_table(m, "users");
    if (!t) { FAIL("users not found"); wlite_model_free(m); return; }
    if (strcmp(wlite_table_name(t), "users") != 0) { FAIL("wrong name"); wlite_model_free(m); return; }
    if (wlite_model_table(m, "nope") != NULL) { FAIL("should be NULL"); wlite_model_free(m); return; }
    wlite_model_free(m);
    PASS();
}

void test_field_introspection(void) {
    TEST("field introspection");
    wlite_model *m = load_str("model X { table \"t\" field id integer { primary_key autoincrement } field name text { not_null unique } }");
    if (!m) { FAIL("load failed"); return; }
    const wlite_table *t = wlite_model_table(m, "t");
    const wlite_field *f = wlite_table_field(t, "id");
    if (!f) { FAIL("id not found"); wlite_model_free(m); return; }
    if (!wlite_field_is_primary_key(f)) { FAIL("id should be PK"); wlite_model_free(m); return; }
    if (!wlite_field_is_autoincrement(f)) { FAIL("id should be autoincrement"); wlite_model_free(m); return; }
    f = wlite_table_field(t, "name");
    if (!f) { FAIL("name not found"); wlite_model_free(m); return; }
    if (!wlite_field_is_unique(f)) { FAIL("name should be unique"); wlite_model_free(m); return; }
    wlite_model_free(m);
    PASS();
}

void test_db_open_close(void) {
    TEST("database open/close");
    wlite_db *db = NULL;
    wlite_result rc = wlite_open(":memory:", &db);
    if (rc != WLITE_OK) { FAIL("open failed"); return; }
    wlite_close(db);
    PASS();
}

void test_db_execute(void) {
    TEST("database execute");
    wlite_db *db = NULL;
    wlite_open(":memory:", &db);
    if (wlite_execute(db, "CREATE TABLE t (id INTEGER PRIMARY KEY, val TEXT)", NULL) != WLITE_OK) { FAIL("create"); wlite_close(db); return; }
    if (wlite_execute(db, "INSERT INTO t VALUES (1, 'hello')", NULL) != WLITE_OK) { FAIL("insert"); wlite_close(db); return; }
    wlite_close(db);
    PASS();
}

void test_stmt_select(void) {
    TEST("statement: select");
    wlite_db *db = NULL;
    wlite_open(":memory:", &db);
    wlite_execute(db, "CREATE TABLE t (id INTEGER PRIMARY KEY, val TEXT)", NULL);
    wlite_execute(db, "INSERT INTO t VALUES (1, 'hello')", NULL);
    wlite_stmt *s = NULL;
    wlite_prepare(db, "SELECT * FROM t", &s);
    if (wlite_step(s) != WLITE_OK) { FAIL("step"); wlite_stmt_finalize(s); wlite_close(db); return; }
    if (wlite_column_int64(s, 0) != 1) { FAIL("id"); wlite_stmt_finalize(s); wlite_close(db); return; }
    if (strcmp(wlite_column_text(s, 1), "hello") != 0) { FAIL("val"); wlite_stmt_finalize(s); wlite_close(db); return; }
    if (wlite_step(s) != WLITE_NOT_FOUND) { FAIL("should be done"); }
    wlite_stmt_finalize(s);
    wlite_close(db);
    PASS();
}

void test_stmt_bind(void) {
    TEST("statement: bind parameters");
    wlite_db *db = NULL;
    wlite_open(":memory:", &db);
    wlite_execute(db, "CREATE TABLE t (id INTEGER PRIMARY KEY, val TEXT, num REAL)", NULL);
    wlite_stmt *s = NULL;
    wlite_prepare(db, "INSERT INTO t VALUES (?, ?, ?)", &s);
    wlite_bind_int64(s, 1, 42);
    wlite_bind_text(s, 2, "test");
    wlite_bind_double(s, 3, 3.14);
    wlite_step(s);
    wlite_stmt_finalize(s);
    wlite_prepare(db, "SELECT * FROM t", &s);
    wlite_step(s);
    int ok = (wlite_column_int64(s, 0) == 42 && strcmp(wlite_column_text(s, 1), "test") == 0 && wlite_column_double(s, 2) == 3.14);
    wlite_stmt_finalize(s);
    wlite_close(db);
    if (!ok) { FAIL("values mismatch"); return; }
    PASS();
}

void test_tx_commit(void) {
    TEST("transaction: commit");
    wlite_db *db = NULL;
    wlite_open(":memory:", &db);
    wlite_execute(db, "CREATE TABLE t (id INTEGER PRIMARY KEY)", NULL);
    wlite_tx *tx = NULL;
    wlite_begin(db, &tx);
    wlite_stmt *s = NULL;
    wlite_prepare(db, "INSERT INTO t VALUES (1)", &s);
    wlite_step(s); wlite_stmt_finalize(s);
    wlite_commit(tx);
    wlite_prepare(db, "SELECT COUNT(*) FROM t", &s);
    wlite_step(s);
    int ok = (wlite_column_int64(s, 0) == 1);
    wlite_stmt_finalize(s); wlite_close(db);
    if (!ok) { FAIL("row count"); return; }
    PASS();
}

void test_tx_rollback(void) {
    TEST("transaction: rollback");
    wlite_db *db = NULL;
    wlite_open(":memory:", &db);
    wlite_execute(db, "CREATE TABLE t (id INTEGER PRIMARY KEY)", NULL);
    wlite_tx *tx = NULL;
    wlite_begin(db, &tx);
    wlite_stmt *s = NULL;
    wlite_prepare(db, "INSERT INTO t VALUES (1)", &s);
    wlite_step(s); wlite_stmt_finalize(s);
    wlite_rollback(tx);
    wlite_prepare(db, "SELECT COUNT(*) FROM t", &s);
    wlite_step(s);
    int ok = (wlite_column_int64(s, 0) == 0);
    wlite_stmt_finalize(s); wlite_close(db);
    if (!ok) { FAIL("row count"); return; }
    PASS();
}

void test_diff_identical(void) {
    TEST("diff: identical schemas");
    wlite_model *m = load_str("model X { table \"t\" field id integer { primary_key } }");
    if (!m) { FAIL("load"); return; }
    wlite_db *db = NULL;
    wlite_open(":memory:", &db);
    wlite_execute(db, "CREATE TABLE t (id INTEGER PRIMARY KEY)", NULL);
    WlPlan *plan = NULL;
    wlite_result rc = wlite_diff(db, m, &plan);
    wlite_close(db);
    wlite_model_free(m);
    if (rc != WLITE_OK) { FAIL("diff failed"); if (plan) wl_plan_free(plan); return; }
    if (plan && wlite_plan_count(plan) > 0) { FAIL("expected no changes"); wl_plan_free(plan); return; }
    if (plan) wl_plan_free(plan);
    PASS();
}

void test_plan_count(void) {
    TEST("plan: step count");
    wlite_model *m = load_str("model X { table \"t\" field id integer { primary_key } }");
    if (!m) { FAIL("load"); return; }
    wlite_db *db = NULL;
    wlite_open(":memory:", &db);
    WlPlan *plan = NULL;
    wlite_diff(db, m, &plan);
    wlite_close(db);
    wlite_model_free(m);
    if (!plan) { FAIL("plan NULL"); return; }
    if (wlite_plan_count(plan) == 0) { FAIL("expected steps"); wl_plan_free(plan); return; }
    wl_plan_free(plan);
    PASS();
}

void test_version(void) {
    TEST("version/abi");
    if (wlite_abi_version() != 1) { FAIL("ABI version"); return; }
    if (!wlite_version()) { FAIL("version NULL"); return; }
    PASS();
}

void test_strerror(void) {
    TEST("strerror");
    if (strcmp(wlite_strerror(WLITE_OK), "OK") != 0) { FAIL("OK"); return; }
    if (strcmp(wlite_strerror(WLITE_ERROR), "general error") != 0) { FAIL("ERROR"); return; }
    PASS();
}

void test_error_report(void) {
    TEST("error reporting");
    wlite_error *err = NULL;
    WlSchema *s = wl_schema_parse("", 0, &err);
    if (s) { FAIL("should be NULL"); wl_schema_free(s); return; }
    if (!err) { FAIL("error not set"); return; }
    if (err->code != WLITE_ERR_NULL_PTR) { FAIL("wrong code"); wlite_error_free(err); return; }
    wlite_error_free(err);
    PASS();
}

int main(void) {
    fprintf(stderr, "wlite test suite\n\n");
    test_parse_basic();
    test_parse_strict();
    test_validate();
    test_table_lookup();
    test_field_introspection();
    test_db_open_close();
    test_db_execute();
    test_stmt_select();
    test_stmt_bind();
    test_tx_commit();
    test_tx_rollback();
    test_diff_identical();
    test_plan_count();
    test_version();
    test_strerror();
    test_error_report();
    fprintf(stderr, "\n%d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
