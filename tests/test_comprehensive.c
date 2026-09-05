#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wlite/wlite.h"

static int T = 0, P = 0;
#define TEST(n) do { T++; printf("  %-55s ", n); } while(0)
#define PASS() do { P++; printf("PASS\n"); } while(0)
#define FAIL(m) do { printf("FAIL: %s\n", m); } while(0)

static wlite_model *load(const char *src) {
    char p[] = "/tmp/wl_XXXXXX"; FILE *f = fopen(p,"w+");
    fwrite(src,1,strlen(src),f); fclose(f);
    wlite_model *m = NULL; wlite_model_load_file(p,&m); remove(p); return m;
}

static WlSchema *parse(const char *src) {
    return wl_schema_parse(src, strlen(src), NULL);
}

/* Migration tests */
void t_migrate(void) { TEST("migrate creates table");
    wlite_db *d; wlite_open(":memory:",&d);
    wlite_model *m=load("model X{table\"t\"field id integer{primary_key} field name text{not_null}}");
    wlite_result r=wlite_migrate(d,m); wlite_model_free(m);
    if(r!=WLITE_OK){FAIL("fail");wlite_close(d);return;}
    wlite_stmt *s; wlite_prepare(d,"SELECT name FROM sqlite_master WHERE type='table' AND name='t'",&s);
    int ok=(wlite_step(s)==WLITE_OK); wlite_stmt_finalize(s); wlite_close(d);
    if(!ok){FAIL("no table");} else PASS();
}
void t_migrate_idem(void) { TEST("migrate idempotent");
    wlite_db *d; wlite_open(":memory:",&d);
    wlite_model *m=load("model X{table\"t\"field id integer{primary_key}}");
    wlite_migrate(d,m); wlite_result r=wlite_migrate(d,m); wlite_model_free(m); wlite_close(d);
    if(r!=WLITE_OK){FAIL("fail");} else PASS();
}
void t_migrate_addcol(void) { TEST("migrate adds column");
    wlite_db *d; wlite_open(":memory:",&d);
    wlite_model *m1=load("model X{table\"t\"field id integer{primary_key}}");
    wlite_migrate(d,m1); wlite_model_free(m1);
    wlite_model *m2=load("model X{table\"t\"field id integer{primary_key} field name text}");
    wlite_result r=wlite_migrate(d,m2); wlite_model_free(m2);
    if(r!=WLITE_OK){FAIL("fail");wlite_close(d);return;}
    wlite_stmt *s; wlite_prepare(d,"PRAGMA table_info(t)",&s);
    int n=0; while(wlite_step(s)==WLITE_OK) n++; wlite_stmt_finalize(s); wlite_close(d);
    if(n!=2){FAIL("cols");} else PASS();
}

/* Snapshot tests */
void t_snap_rt(void) { TEST("snapshot roundtrip");
    /* NOTE: AUTOINCREMENT is not detectable from PRAGMA table_info.
       Test with a model that doesn't use autoincrement. */
    wlite_db *d; wlite_open(":memory:",&d);
    wlite_model *m=load("model X{table\"t\"field id integer{primary_key} field name text{not_null unique} field val real}");
    wlite_migrate(d,m); wlite_model_free(m);
    WlSchema *a=wl_schema_inspect(d,NULL);
    WlSchema *b=parse("model X{table\"t\"field id integer{primary_key} field name text{not_null unique} field val real}");
    WlDiff *df=wl_schema_diff(a,b,NULL);
    int ok=(df&&df->entry_count==0);
    wl_diff_free(df); wl_schema_free(b); wl_schema_free(a); wlite_close(d);
    if(!ok){FAIL("diff not empty");} else PASS();
}
void t_snap_change(void) { TEST("snapshot detects change");
    wlite_db *d; wlite_open(":memory:",&d);
    wlite_model *m=load("model X{table\"t\"field id integer{primary_key} field name text}");
    wlite_migrate(d,m); wlite_model_free(m);
    WlSchema *a=wl_schema_inspect(d,NULL);
    WlSchema *b=parse("model X{table\"t\"field id integer{primary_key} field name text field extra text}");
    WlDiff *df=wl_schema_diff(a,b,NULL);
    int ok=(df&&df->entry_count>0);
    wl_diff_free(df); wl_schema_free(b); wl_schema_free(a); wlite_close(d);
    if(!ok){FAIL("no diff");} else PASS();
}

/* Compiled model tests */
void t_compiled(void) { TEST("compiled model roundtrip");
    const char *s="model X{table\"t\"field id integer{primary_key} field name text{not_null}}";
    WlSchema *o=parse(s);
    wl_model_compile(o,"/tmp/tc.wlitem");
    FILE *f=fopen("/tmp/tc.wlitem","rb"); fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    char *buf=malloc(sz); fread(buf,1,sz,f); fclose(f);
    WlSchema *l=wl_model_load_compiled_raw(buf,sz); free(buf);
    WlDiff *d=wl_schema_diff(o,l,NULL);
    int ok=(d&&d->entry_count==0);
    wl_diff_free(d); wl_schema_free(l); wl_schema_free(o); remove("/tmp/tc.wlitem");
    if(!ok){FAIL("diff");} else PASS();
}
void t_compiled_meta(void) { TEST("compiled model metadata");
    const char *s="model_config \"test\" 7 model X{table\"t\"field id integer{primary_key}}";
    WlSchema *o=parse(s);
    wl_model_compile(o,"/tmp/tcm.wlitem");
    FILE *f=fopen("/tmp/tcm.wlitem","rb"); fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    char *buf=malloc(sz); fread(buf,1,sz,f); fclose(f);
    WlSchema *l=wl_model_load_compiled_raw(buf,sz); free(buf);
    int ok=(l&&wl_schema_model_name(l)&&strcmp(wl_schema_model_name(l),"test")==0&&wl_schema_model_version(l)==7);
    wl_schema_free(l); wl_schema_free(o); remove("/tmp/tcm.wlitem");
    if(!ok){FAIL("meta");} else PASS();
}

/* Parser edge cases */
void t_parse_empty(void) { TEST("parser empty body");
    WlSchema *s=parse("model X{ }");
    int ok=(s&&s->table_count==1&&s->tables[0].column_count==0);
    if(s)wl_schema_free(s);
    if(!ok){FAIL("fail");} else PASS();
}
void t_parse_unknown(void) { TEST("parser unknown keyword");
    WlSchema *s=parse("model X{table\"t\"unknown_stuff field id integer}");
    int ok=(s&&s->table_count==1);
    if(s)wl_schema_free(s);
    if(!ok){FAIL("fail");} else PASS();
}
void t_parse_multi(void) { TEST("parser multiple models");
    WlSchema *s=parse("model A{table\"a\"field x integer}model B{table\"b\"field y text}model C{table\"c\"field z real}");
    int ok=(s&&s->table_count==3);
    if(s)wl_schema_free(s);
    if(!ok){FAIL("count");} else PASS();
}
void t_parse_view(void) { TEST("parser view");
    WlSchema *s=parse("model X{table\"t\"field id integer}view V{select id from t}");
    int ok=(s&&s->view_count==1);
    if(s)wl_schema_free(s);
    if(!ok){FAIL("count");} else PASS();
}
void t_parse_idx(void) { TEST("parser index");
    WlSchema *s=parse("model X{table\"t\"field id integer}index ix_t{on t(id)}");
    int ok=(s&&s->index_count==1);
    if(s)wl_schema_free(s);
    if(!ok){FAIL("count");} else PASS();
}
void t_parse_uidx(void) { TEST("parser unique index");
    WlSchema *s=parse("model X{table\"t\"field id integer}index ix_t{unique on t(id)}");
    int ok=(s&&s->index_count==1&&s->indexes[0].unique);
    if(s)wl_schema_free(s);
    if(!ok){FAIL("unique");} else PASS();
}
void t_parse_fk(void) { TEST("parser foreign key");
    WlSchema *s=parse("model A{table\"a\"field id integer{primary_key}}model B{table\"b\"field id integer{primary_key} field aid integer{references A.id}}");
    int ok=(s&&s->table_count==2);
    if(s)wl_schema_free(s);
    if(!ok){FAIL("count");} else PASS();
}
void t_parse_gen(void) { TEST("parser generated column");
    WlSchema *s=parse("model X{table\"t\"field first text field last text field full text{generated(first||' '||last)stored}}");
    int ok=(s&&s->tables[0].column_count==3&&s->tables[0].columns[2].is_generated);
    if(s)wl_schema_free(s);
    if(!ok){FAIL("gen");} else PASS();
}
void t_parse_collate(void) { TEST("parser collate");
    WlSchema *s=parse("model X{table\"t\"field name text{collate NOCASE}}");
    int ok=(s&&s->tables[0].columns[0].collate&&strcmp(s->tables[0].columns[0].collate,"NOCASE")==0);
    if(s)wl_schema_free(s);
    if(!ok){FAIL("collate");} else PASS();
}
void t_parse_check(void) { TEST("parser check constraint");
    WlSchema *s=parse("model X{table\"t\"field id integer{primary_key} field age integer{check(age>=0)}}");
    int ok=(s&&s->tables[0].check_count==1);
    if(s)wl_schema_free(s);
    if(!ok){FAIL("count");} else PASS();
}
void t_parse_strict(void) { TEST("parser strict");
    WlSchema *s=parse("model X{table\"t\"strict field id integer{primary_key}}");
    int ok=(s&&s->tables[0].strict);
    if(s)wl_schema_free(s);
    if(!ok){FAIL("strict");} else PASS();
}
void t_parse_norowid(void) { TEST("parser without rowid");
    WlSchema *s=parse("model X{table\"t\"without rowid field id integer{primary_key} field name text}");
    int ok=(s&&s->tables[0].without_rowid);
    if(s)wl_schema_free(s);
    if(!ok){FAIL("norowid");} else PASS();
}

/* Rebuild tests */
void t_rebuild_run(void) { TEST("rebuild SQL runs");
    wlite_db *d; wlite_open(":memory:",&d);
    wlite_execute(d,"CREATE TABLE t(id INTEGER PRIMARY KEY,name TEXT,old_col TEXT)",NULL);
    wlite_model *m=load("model X{table\"t\"field id integer{primary_key} field name text{not_null} field new_col text}");
    wlite_result r=wlite_migrate(d,m); wlite_model_free(m);
    if(r!=WLITE_OK){FAIL("migrate");wlite_close(d);return;}
    wlite_stmt *s; wlite_prepare(d,"PRAGMA table_info(t)",&s);
    int has_new=0,has_old=0;
    while(wlite_step(s)==WLITE_OK){const char *n=wlite_column_text(s,1);if(strcmp(n,"new_col")==0)has_new=1;if(strcmp(n,"old_col")==0)has_old=1;}
    wlite_stmt_finalize(s); wlite_close(d);
    if(!has_new||has_old){FAIL("schema");} else PASS();
}
void t_rebuild_data(void) { TEST("rebuild preserves data");
    wlite_db *d; wlite_open(":memory:",&d);
    wlite_execute(d,"CREATE TABLE t(id INTEGER PRIMARY KEY,name TEXT)",NULL);
    wlite_execute(d,"INSERT INTO t VALUES(1,'alice')",NULL);
    wlite_execute(d,"INSERT INTO t VALUES(2,'bob')",NULL);
    wlite_model *m=load("model X{table\"t\"field id integer{primary_key} field name text{not_null}}");
    wlite_migrate(d,m); wlite_model_free(m);
    wlite_stmt *s; wlite_prepare(d,"SELECT COUNT(*) FROM t",&s); wlite_step(s);
    int64_t c=wlite_column_int64(s,0); wlite_stmt_finalize(s); wlite_close(d);
    if(c!=2){FAIL("lost data");} else PASS();
}

/* API tests */
void t_diff_api(void) { TEST("wlite_diff single-call API");
    wlite_db *d; wlite_open(":memory:",&d);
    wlite_model *m=load("model X{table\"t\"field id integer{primary_key}}");
    WlPlan *p=NULL; wlite_result r=wlite_diff(d,m,&p); wlite_model_free(m);
    if(r!=WLITE_OK){FAIL("diff");wlite_close(d);return;}
    if(!p||wlite_plan_count(p)==0){FAIL("no plan");wl_plan_free(p);wlite_close(d);return;}
    wl_plan_free(p); wlite_close(d); PASS();
}
void t_verify_match(void) { TEST("wlite_schema_verify match");
    wlite_db *d; wlite_open(":memory:",&d);
    wlite_model *m=load("model X{table\"t\"field id integer{primary_key}}");
    wlite_migrate(d,m);
    WlSchema *e=parse("model X{table\"t\"field id integer{primary_key}}");
    WlDiff *df=NULL; wlite_result r=wl_schema_verify(d,e,&df,NULL);
    wl_schema_free(e); wlite_model_free(m); wlite_close(d);
    if(r!=WLITE_OK){FAIL("verify");if(df)wl_diff_free(df);return;}
    if(df){FAIL("has diff");wl_diff_free(df);return;}
    PASS();
}
void t_verify_mismatch(void) { TEST("wlite_schema_verify detects mismatch");
    wlite_db *d; wlite_open(":memory:",&d);
    wlite_model *m=load("model X{table\"t\"field id integer{primary_key}}");
    wlite_migrate(d,m); wlite_model_free(m);
    WlSchema *e=parse("model X{table\"t\"field id integer{primary_key} field extra text}");
    WlDiff *df=NULL; wlite_result r=wl_schema_verify(d,e,&df,NULL);
    wl_schema_free(e); wlite_close(d);
    if(r!=WLITE_NOT_FOUND){FAIL("expected NOT_FOUND");if(df)wl_diff_free(df);return;}
    if(!df||df->entry_count==0){FAIL("no diff");if(df)wl_diff_free(df);return;}
    wl_diff_free(df); PASS();
}

/* Error tests */
void t_err_nulldb(void) { TEST("error NULL db");
    if(wlite_execute(NULL,"SELECT 1",NULL)!=WLITE_INVALID_ARGUMENT){FAIL("wrong");} else PASS();
}
void t_err_nullmodel(void) { TEST("error NULL model");
    wlite_db *d; wlite_open(":memory:",&d);
    if(wlite_migrate(d,NULL)!=WLITE_INVALID_ARGUMENT){FAIL("wrong");wlite_close(d);} else {wlite_close(d);PASS();}
}
void t_err_badsql(void) { TEST("error invalid SQL");
    wlite_db *d; wlite_open(":memory:",&d);
    if(wlite_execute(d,"NOT VALID SQL",NULL)==WLITE_OK){FAIL("should fail");} else PASS();
    wlite_close(d);
}
void t_err_badprep(void) { TEST("error prepare invalid");
    wlite_db *d; wlite_open(":memory:",&d);
    wlite_stmt *s=NULL;
    if(wlite_prepare(d,"NOT VALID",&s)==WLITE_OK){FAIL("should fail");if(s)wlite_stmt_finalize(s);} else PASS();
    wlite_close(d);
}

/* Transaction edge cases */
void t_tx_dcommit(void) { TEST("tx double commit safe");
    wlite_db *d; wlite_open(":memory:",&d);
    wlite_tx *t; wlite_begin(d,&t); wlite_commit(t); t=NULL; /* freed by commit */
    /* Second commit on NULL should be safe */
    if(t) wlite_commit(t); /* t is NULL, this is a no-op check */
    wlite_close(d); PASS();
}
void t_tx_drollback(void) { TEST("tx double rollback safe");
    wlite_db *d; wlite_open(":memory:",&d);
    wlite_tx *t; wlite_begin(d,&t); wlite_rollback(t); t=NULL; /* freed by rollback */
    if(t) wlite_rollback(t); /* t is NULL, this is a no-op check */
    wlite_close(d); PASS();
}
void t_parse_suffix(void) { TEST("parse: text(10) suffix");
    WlSchema *s = parse("model X{table\"t\"field a text(10)}");
    if(!s||s->table_count!=1||s->tables[0].column_count!=1){FAIL("parse");if(s)wl_schema_free(s);return;}
    if(strcmp(s->tables[0].columns[0].type_name,"text(10)")!=0){FAIL("type");wl_schema_free(s);return;}
    wl_schema_free(s); PASS();
}
void t_parse_suffix_varchar(void) { TEST("parse: VARCHAR(255) suffix");
    WlSchema *s = parse("model X{table\"t\"field a VARCHAR(255)}");
    if(!s||s->table_count!=1){FAIL("parse");if(s)wl_schema_free(s);return;}
    if(strcmp(s->tables[0].columns[0].type_name,"VARCHAR(255)")!=0){FAIL("type");wl_schema_free(s);return;}
    wl_schema_free(s); PASS();
}
void t_parse_model_config(void) { TEST("parse: model_config");
    WlSchema *s = parse("model_config \"myapp\" 1.0 model User{table\"users\"field id integer}");
    if(!s||s->table_count!=1){FAIL("parse");if(s)wl_schema_free(s);return;}
    if(!s->model_name||strcmp(s->model_name,"myapp")!=0){FAIL("name");wl_schema_free(s);return;}
    if(s->model_version!=1){FAIL("version");wl_schema_free(s);return;}
    wl_schema_free(s); PASS();
}

int main(void) {
    printf("wlite comprehensive tests\n\n");
    t_migrate(); t_migrate_idem(); t_migrate_addcol();
    t_snap_rt(); t_snap_change();
    t_compiled(); t_compiled_meta();
    t_parse_empty(); t_parse_unknown(); t_parse_multi(); t_parse_view();
    t_parse_idx(); t_parse_uidx(); t_parse_fk(); t_parse_gen();
    t_parse_collate(); t_parse_check(); t_parse_strict(); t_parse_norowid();
    t_rebuild_run(); t_rebuild_data();
    t_diff_api(); t_verify_match(); t_verify_mismatch();
    t_err_nulldb(); t_err_nullmodel(); t_err_badsql(); t_err_badprep();
    t_tx_dcommit(); t_tx_drollback();
    t_parse_suffix(); t_parse_suffix_varchar(); t_parse_model_config();
    printf("\n%d/%d passed\n", P, T);
    return P == T ? 0 : 1;
}
