/*
 * conformance.c — Cross-language conformance test definitions
 *
 * These tests verify that the C API behaves correctly.
 * The same semantic tests should be implemented in Rust, Python, C++, Go, and Zig.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wlite/wlite.h"

static int tests_run = 0, tests_passed = 0;
#define TEST(n) do { tests_run++; printf("  %-50s ", n); } while(0)
#define PASS() do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while(0)

/* ── Model semantics ──────────────────────────────────────────────────── */

static const char *BASIC_MODEL =
    "model User {\n"
    "    table \"users\"\n"
    "    field id integer { primary_key autoincrement }\n"
    "    field name text { not_null unique }\n"
    "    field email text\n"
    "    field active boolean { not_null default true }\n"
    "}";

static const char *TWO_TABLE_MODEL =
    "model User { table \"users\" field id integer { primary_key } field name text }\n"
    "model Post { table \"posts\" field id integer { primary_key } field user_id integer { references User.id } }";

void test_model_parse(void) {
    TEST("model: parse basic");
    wlite_model *m = NULL;
    wlite_model_load_memory(BASIC_MODEL, strlen(BASIC_MODEL), &m);
    if (!m) { FAIL("load failed"); return; }
    if (wlite_model_table_count(m) != 1) { FAIL("wrong table count"); wlite_model_free(m); return; }
    wlite_model_free(m);
    PASS();
}

void test_model_validate(void) {
    TEST("model: validate");
    wlite_model *m = NULL;
    wlite_model_load_memory(BASIC_MODEL, strlen(BASIC_MODEL), &m);
    wlite_result rc = wlite_model_validate(m);
    wlite_model_free(m);
    if (rc != WLITE_OK) { FAIL("validate failed"); return; }
    PASS();
}

void test_model_introspection(void) {
    TEST("model: introspection");
    wlite_model *m = NULL;
    wlite_model_load_memory(TWO_TABLE_MODEL, strlen(TWO_TABLE_MODEL), &m);
    if (wlite_model_table_count(m) != 2) { FAIL("wrong count"); wlite_model_free(m); return; }
    const wlite_table *t = wlite_model_table(m, "users");
    if (!t) { FAIL("users not found"); wlite_model_free(m); return; }
    if (wlite_table_field_count(t) != 2) { FAIL("wrong field count"); wlite_model_free(m); return; }
    const wlite_field *f = wlite_table_field(t, "id");
    if (!f || !wlite_field_is_primary_key(f)) { FAIL("id should be PK"); wlite_model_free(m); return; }
    wlite_model_free(m);
    PASS();
}

/* ── Value semantics ──────────────────────────────────────────────────── */

void test_null_value(void) {
    TEST("value: NULL");
    wlite_db *db; wlite_open(":memory:", &db);
    wlite_execute(db, "CREATE TABLE t (id INTEGER, val TEXT)", NULL);
    wlite_execute(db, "INSERT INTO t VALUES (1, NULL)", NULL);
    wlite_stmt *s; wlite_prepare(db, "SELECT val FROM t", &s);
    wlite_step(s);
    if (wlite_column_type(s, 0) != WLITE_TYPE_NULL) { FAIL("wrong type"); wlite_stmt_finalize(s); wlite_close(db); return; }
    wlite_stmt_finalize(s); wlite_close(db);
    PASS();
}

void test_integer_value(void) {
    TEST("value: INTEGER");
    wlite_db *db; wlite_open(":memory:", &db);
    wlite_execute(db, "CREATE TABLE t (v INTEGER)", NULL);
    wlite_execute(db, "INSERT INTO t VALUES (42)", NULL);
    wlite_stmt *s; wlite_prepare(db, "SELECT v FROM t", &s);
    wlite_step(s);
    if (wlite_column_type(s, 0) != WLITE_TYPE_INTEGER || wlite_column_int64(s, 0) != 42)
        { FAIL("wrong value"); wlite_stmt_finalize(s); wlite_close(db); return; }
    wlite_stmt_finalize(s); wlite_close(db);
    PASS();
}

void test_text_value(void) {
    TEST("value: TEXT");
    wlite_db *db; wlite_open(":memory:", &db);
    wlite_execute(db, "CREATE TABLE t (v TEXT)", NULL);
    wlite_execute(db, "INSERT INTO t VALUES ('hello')", NULL);
    wlite_stmt *s; wlite_prepare(db, "SELECT v FROM t", &s);
    wlite_step(s);
    if (wlite_column_type(s, 0) != WLITE_TYPE_TEXT || strcmp(wlite_column_text(s, 0), "hello") != 0)
        { FAIL("wrong value"); wlite_stmt_finalize(s); wlite_close(db); return; }
    wlite_stmt_finalize(s); wlite_close(db);
    PASS();
}

/* ── Transaction semantics ────────────────────────────────────────────── */

void test_tx_commit(void) {
    TEST("transaction: commit");
    wlite_db *db; wlite_open(":memory:", &db);
    wlite_execute(db, "CREATE TABLE t (id INTEGER)", NULL);
    wlite_tx *tx; wlite_begin(db, &tx);
    wlite_stmt *s; wlite_prepare(db, "INSERT INTO t VALUES (1)", &s);
    wlite_step(s); wlite_stmt_finalize(s);
    wlite_commit(tx);
    wlite_prepare(db, "SELECT COUNT(*) FROM t", &s);
    wlite_step(s);
    int ok = (wlite_column_int64(s, 0) == 1);
    wlite_stmt_finalize(s); wlite_close(db);
    if (!ok) { FAIL("count"); return; }
    PASS();
}

void test_tx_rollback(void) {
    TEST("transaction: rollback");
    wlite_db *db; wlite_open(":memory:", &db);
    wlite_execute(db, "CREATE TABLE t (id INTEGER)", NULL);
    wlite_tx *tx; wlite_begin(db, &tx);
    wlite_stmt *s; wlite_prepare(db, "INSERT INTO t VALUES (1)", &s);
    wlite_step(s); wlite_stmt_finalize(s);
    wlite_rollback(tx);
    wlite_prepare(db, "SELECT COUNT(*) FROM t", &s);
    wlite_step(s);
    int ok = (wlite_column_int64(s, 0) == 0);
    wlite_stmt_finalize(s); wlite_close(db);
    if (!ok) { FAIL("count"); return; }
    PASS();
}

void test_tx_savepoint(void) {
    TEST("transaction: savepoint");
    wlite_db *db; wlite_open(":memory:", &db);
    wlite_execute(db, "CREATE TABLE t (id INTEGER)", NULL);
    wlite_tx *tx; wlite_begin(db, &tx);
    wlite_stmt *s;
    wlite_prepare(db, "INSERT INTO t VALUES (1)", &s); wlite_step(s); wlite_stmt_finalize(s);
    wlite_savepoint(tx, "sp1");
    wlite_prepare(db, "INSERT INTO t VALUES (2)", &s); wlite_step(s); wlite_stmt_finalize(s);
    wlite_rollback_to(tx, "sp1");
    wlite_prepare(db, "SELECT COUNT(*) FROM t", &s); wlite_step(s);
    int ok = (wlite_column_int64(s, 0) == 1);
    wlite_stmt_finalize(s);
    wlite_release(tx, "sp1");
    wlite_commit(tx);
    wlite_close(db);
    if (!ok) { FAIL("count after rollback_to"); return; }
    PASS();
}

/* ── Schema behavior ──────────────────────────────────────────────────── */

void test_schema_create_table(void) {
    TEST("schema: create table");
    wlite_db *db; wlite_open(":memory:", &db);
    wlite_model *m = NULL;
    wlite_model_load_memory(BASIC_MODEL, strlen(BASIC_MODEL), &m);
    wlite_result rc = wlite_migrate(db, m);
    wlite_model_free(m);
    if (rc != WLITE_OK) { FAIL("migrate"); wlite_close(db); return; }
    wlite_stmt *s;
    wlite_prepare(db, "SELECT name FROM sqlite_master WHERE type='table' AND name='users'", &s);
    int ok = (wlite_step(s) == WLITE_OK);
    wlite_stmt_finalize(s); wlite_close(db);
    if (!ok) { FAIL("table not created"); return; }
    PASS();
}

void test_schema_idempotent(void) {
    TEST("schema: idempotent migrate");
    wlite_db *db; wlite_open(":memory:", &db);
    wlite_model *m = NULL;
    wlite_model_load_memory(BASIC_MODEL, strlen(BASIC_MODEL), &m);
    wlite_migrate(db, m);
    wlite_result rc = wlite_migrate(db, m);
    wlite_model_free(m);
    wlite_close(db);
    if (rc != WLITE_OK) { FAIL("second migrate failed"); return; }
    PASS();
}

/* ── Main ─────────────────────────────────────────────────────────────── */

int main(void) {
    printf("wlite conformance tests\n\n");
    test_model_parse();
    test_model_validate();
    test_model_introspection();
    test_null_value();
    test_integer_value();
    test_text_value();
    test_tx_commit();
    test_tx_rollback();
    test_tx_savepoint();
    test_schema_create_table();
    test_schema_idempotent();
    printf("\n%d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
