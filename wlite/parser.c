/*
 * parser.c — Parse .wlite DSL into WlSchema
 */

#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include "wlite/wlite.h"
#include "internal.h"

typedef enum {
    TOK_EOF, TOK_IDENT, TOK_LBRACE, TOK_RBRACE,
    TOK_LPAREN, TOK_RPAREN, TOK_COMMA, TOK_SEMICOLON,
    TOK_STRING, TOK_NUMBER, TOK_DOT, TOK_ERROR,
} TokenType;

typedef struct { const char *start; const char *current; int line; } Lexer;
typedef struct { TokenType type; const char *start; size_t length; int line; } Token;

static void lexer_init(Lexer *l, const char *s) { l->start = s; l->current = s; l->line = 1; }
static int is_alpha(char c) { return isalpha(c) || c == '_'; }
static int is_alnum(char c) { return isalnum(c) || c == '_'; }

static void skip_ws(Lexer *l) {
    for (;;) {
        char c = *l->current;
        if (c == ' ' || c == '\t' || c == '\r') l->current++;
        else if (c == '\n') { l->current++; l->line++; }
        else if (c == '#') { while (*l->current && *l->current != '\n') l->current++; }
        else if (c == '/' && l->current[1] == '*') {
            l->current += 2;
            while (*l->current && !(l->current[0] == '*' && l->current[1] == '/')) l->current++;
            if (*l->current) l->current += 2;
        } else break;
    }
}

static Token next_token(Lexer *l) {
    skip_ws(l);
    Token t = { .start = l->current, .line = l->line };
    if (*l->current == '\0') { t.type = TOK_EOF; return t; }
    char c = *l->current++;
    switch (c) {
        case '{': t.type = TOK_LBRACE; t.length = 1; return t;
        case '}': t.type = TOK_RBRACE; t.length = 1; return t;
        case '(': t.type = TOK_LPAREN; t.length = 1; return t;
        case ')': t.type = TOK_RPAREN; t.length = 1; return t;
        case ',': t.type = TOK_COMMA; t.length = 1; return t;
        case ';': t.type = TOK_SEMICOLON; t.length = 1; return t;
        case '.': t.type = TOK_DOT; t.length = 1; return t;
        case '\'': case '"': {
            char q = c;
            while (*l->current && *l->current != q) {
                if (*l->current == '\\') l->current++;
                l->current++;
            }
            if (*l->current) l->current++;
            t.type = TOK_STRING; t.length = (size_t)(l->current - t.start); return t;
        }
        default:
            if (isdigit(c)) { while (isdigit(*l->current)) l->current++; t.type = TOK_NUMBER; t.length = (size_t)(l->current - t.start); return t; }
            if (is_alpha(c)) { while (is_alnum(*l->current)) l->current++; t.type = TOK_IDENT; t.length = (size_t)(l->current - t.start); return t; }
            t.type = TOK_ERROR; t.length = 1; return t;
    }
}

static Token peek_token(Lexer *l) { Lexer save = *l; Token t = next_token(l); *l = save; return t; }

static int match_ident_ci(Lexer *l, const char *word) {
    Token t = peek_token(l);
    if (t.type != TOK_IDENT) return 0;
    size_t len = strlen(word);
    if (t.length != len) return 0;
    for (size_t i = 0; i < len; i++)
        if (tolower((unsigned char)t.start[i]) != tolower((unsigned char)word[i])) return 0;
    next_token(l); return 1;
}

static char *token_to_string(Token t);
static char *expect_ident(Lexer *l, wlite_error **err) {
    Token t = next_token(l);
    if (t.type != TOK_IDENT) {
        if (err) { *err = calloc(1, sizeof(wlite_error)); (*err)->code = WLITE_ERR_SYNTAX;
            (*err)->message = strdup("expected identifier"); (*err)->line = t.line; }
        return NULL;
    }
    return token_to_string(t);
}

static char *expect_string(Lexer *l, wlite_error **err) {
    Token t = next_token(l);
    if (t.type != TOK_STRING) {
        if (err) { *err = calloc(1, sizeof(wlite_error)); (*err)->code = WLITE_ERR_SYNTAX;
            (*err)->message = strdup("expected string"); (*err)->line = t.line; }
        return NULL;
    }
    char *s = malloc(t.length - 1);
    memcpy(s, t.start + 1, t.length - 2); s[t.length - 2] = '\0';
    return s;
}

static int expect_token(Lexer *l, TokenType type, wlite_error **err) {
    Token t = next_token(l);
    if (t.type != type) {
        if (err) { *err = calloc(1, sizeof(wlite_error)); (*err)->code = WLITE_ERR_SYNTAX;
            (*err)->message = strdup("unexpected token"); (*err)->line = t.line; }
        return 0;
    }
    return 1;
}


static char *token_to_string(Token t) {
    char *s = malloc(t.length + 1); memcpy(s, t.start, t.length); s[t.length] = '\0'; return s;
}

static char *consume_balanced(Lexer *l) {
    int depth = 0; const char *start = l->current;
    while (*l->current) {
        if (*l->current == '(') depth++;
        else if (*l->current == ')') { if (depth == 0) break; depth--; }
        l->current++;
    }
    size_t len = (size_t)(l->current - start);
    char *s = malloc(len + 1); memcpy(s, start, len); s[len] = '\0';
    if (*l->current == ')') l->current++;
    return s;
}

static char *consume_expr(Lexer *l) {
    while (*l->current == ' ' || *l->current == '\t') l->current++;
    const char *start = l->current;
    int depth = 0;
    while (*l->current) {
        if (*l->current == '(') depth++;
        else if (*l->current == ')') { if (depth == 0) break; depth--; }
        else if (*l->current == '}' && depth == 0) break;
        else if (depth == 0 && *l->current == ',') break;
        l->current++;
    }
    size_t len = (size_t)(l->current - start);
    while (len > 0 && (start[len-1] == ' ' || start[len-1] == '\t' || start[len-1] == '\n')) len--;
    char *s = malloc(len + 1); memcpy(s, start, len); s[len] = '\0';
    return s;
}

wlite_col_type resolve_affinity(const char *type) {
    if (!type) return WL_COL_BLOB;
    char upper[64]; size_t len = strlen(type);
    if (len >= sizeof(upper)) len = sizeof(upper) - 1;
    for (size_t i = 0; i < len; i++) upper[i] = toupper((unsigned char)type[i]);
    upper[len] = '\0';
    if (strstr(upper, "INT")) return WL_COL_INTEGER;
    if (strstr(upper, "CHAR") || strstr(upper, "CLOB") || strstr(upper, "TEXT")) return WL_COL_TEXT;
    if (strstr(upper, "BLOB") || len == 0) return WL_COL_BLOB;
    if (strstr(upper, "REAL") || strstr(upper, "FLOA") || strstr(upper, "DOUB")) return WL_COL_REAL;
    if (strstr(upper, "ANY")) return WL_COL_ANY;
    return WL_COL_NONE;
}

static int parse_field(Lexer *l, WlTable *table, wlite_error **err) {
    WlColumn col = {0};
    col.name = expect_ident(l, err);
    if (!col.name) return 0;

    Token type_tok = next_token(l);
    if (type_tok.type != TOK_IDENT) {
        if (err) { (*err)->code = WLITE_ERR_SYNTAX; (*err)->message = strdup("expected field type"); (*err)->line = type_tok.line; }
        return 0;
    }

    /* Check for parenthesized suffix like VARCHAR(255) */
    if (peek_token(l).type == TOK_LPAREN) {
        next_token(l); /* consume ( */
        int depth = 1;
        const char *paren_start = l->current - 1;
        while (*l->current) {
            char c = *l->current;
            if (c == '(') depth++;
            else if (c == ')') { if (depth == 1) break; depth--; }
            l->current++;
        }
        size_t suffix_len = (size_t)(l->current - paren_start + 1);
        l->current++;
        size_t base_len = type_tok.length;
        col.type_name = malloc(base_len + suffix_len + 1);
        memcpy(col.type_name, type_tok.start, base_len);
        memcpy(col.type_name + base_len, paren_start, suffix_len);
        col.type_name[base_len + suffix_len] = '\0';
    } else {
        col.type_name = token_to_string(type_tok);
    }
    col.affinity = resolve_affinity(col.type_name);

    /* Parse field attributes — either bare or inside { } */
    for (;;) {
        Token t = peek_token(l);
        if (t.type == TOK_RBRACE || t.type == TOK_EOF) break;

        if (t.type == TOK_LBRACE) {
            next_token(l); /* consume { */
            while (peek_token(l).type != TOK_RBRACE && peek_token(l).type != TOK_EOF) {
                Token a = peek_token(l);
                if (a.type != TOK_IDENT) { next_token(l); continue; }
                if (match_ident_ci(l, "primary_key")) col.primary_key = 1;
                else if (match_ident_ci(l, "autoincrement")) col.autoincrement = 1;
                else if (match_ident_ci(l, "not_null")) col.not_null = 1;
                else if (match_ident_ci(l, "not")) { match_ident_ci(l, "null"); col.not_null = 1; }
                else if (match_ident_ci(l, "unique")) col.is_unique = 1;
                else if (match_ident_ci(l, "default")) col.default_expr = consume_expr(l);
                else if (match_ident_ci(l, "collate")) col.collate = expect_ident(l, err);
                else if (match_ident_ci(l, "references")) {
                    col.fk_table = expect_ident(l, err);
                    if (expect_token(l, TOK_DOT, err)) col.fk_column = expect_ident(l, err);
                } else if (match_ident_ci(l, "check")) {
                    if (expect_token(l, TOK_LPAREN, err)) {
                        char *_ck_expr = consume_balanced(l);
                        table->checks = realloc(table->checks, (table->check_count + 1) * sizeof(WlCheck));
                        table->checks[table->check_count++] = (WlCheck){ .expression = _ck_expr };
                    }
                } else if (match_ident_ci(l, "generated")) {
                    if (expect_token(l, TOK_LPAREN, err)) { col.generated_expr = consume_balanced(l); col.is_generated = 1; }
                    if (match_ident_ci(l, "stored")) col.is_stored = 1;
                    else if (match_ident_ci(l, "virtual")) col.is_stored = 0;
                    else col.is_stored = 1;
                } else { next_token(l); }
            }
            if (peek_token(l).type == TOK_RBRACE) next_token(l);
            continue;
        }

        /* Bare attributes */
        if (match_ident_ci(l, "primary_key")) col.primary_key = 1;
        else if (match_ident_ci(l, "autoincrement")) col.autoincrement = 1;
        else if (match_ident_ci(l, "not_null")) col.not_null = 1;
        else if (match_ident_ci(l, "not")) { match_ident_ci(l, "null"); col.not_null = 1; }
        else if (match_ident_ci(l, "unique")) col.is_unique = 1;
        else break;
    }

    table->columns = realloc(table->columns, (table->column_count + 1) * sizeof(WlColumn));
    table->columns[table->column_count++] = col;
    return 1;
}

static int parse_model(Lexer *l, WlSchema *schema, wlite_error **err) {
    WlTable table = {0};
    table.name = expect_ident(l, err);
    if (!table.name) return 0;
    if (!expect_token(l, TOK_LBRACE, err)) return 0;

    while (peek_token(l).type != TOK_RBRACE && peek_token(l).type != TOK_EOF) {
        if (match_ident_ci(l, "table")) { table.name = expect_string(l, err); continue; }
        if (match_ident_ci(l, "strict")) { table.strict = 1; continue; }
        if (match_ident_ci(l, "without")) { if (match_ident_ci(l, "rowid")) table.without_rowid = 1; continue; }
        if (match_ident_ci(l, "comment")) { table.comment = expect_string(l, err); continue; }

        if (match_ident_ci(l, "primary_key")) {
            if (expect_token(l, TOK_LPAREN, err)) {
                while (peek_token(l).type != TOK_RPAREN) {
                    char *c = expect_ident(l, err);
                    if (c) { table.primary_key.columns = realloc(table.primary_key.columns, (table.primary_key.column_count+1)*sizeof(char*)); table.primary_key.columns[table.primary_key.column_count++] = c; }
                    if (peek_token(l).type == TOK_COMMA) next_token(l);
                }
                expect_token(l, TOK_RPAREN, err);
            }
            continue;
        }
        if (match_ident_ci(l, "unique")) {
            if (expect_token(l, TOK_LPAREN, err)) {
                WlUnique uq = {0};
                while (peek_token(l).type != TOK_RPAREN) {
                    char *c = expect_ident(l, err);
                    if (c) { uq.columns = realloc(uq.columns, (uq.column_count+1)*sizeof(char*)); uq.columns[uq.column_count++] = c; }
                    if (peek_token(l).type == TOK_COMMA) next_token(l);
                }
                expect_token(l, TOK_RPAREN, err);
                table.uniques = realloc(table.uniques, (table.unique_count+1)*sizeof(WlUnique));
                table.uniques[table.unique_count++] = uq;
            }
            continue;
        }
        if (match_ident_ci(l, "check")) {
            if (expect_token(l, TOK_LPAREN, err)) {
                char *expr = consume_balanced(l);
                table.checks = realloc(table.checks, (table.check_count+1)*sizeof(WlCheck));
                table.checks[table.check_count++] = (WlCheck){ .expression = expr };
            }
            continue;
        }
        if (match_ident_ci(l, "foreign_key")) {
            if (expect_token(l, TOK_LPAREN, err)) {
                WlForeignKey fk = {0};
                while (peek_token(l).type != TOK_RPAREN) {
                    char *c = expect_ident(l, err);
                    if (c) { fk.columns = realloc(fk.columns, (fk.column_count+1)*sizeof(char*)); fk.columns[fk.column_count++] = c; }
                    if (peek_token(l).type == TOK_COMMA) next_token(l);
                }
                expect_token(l, TOK_RPAREN, err);
                if (match_ident_ci(l, "references")) {
                    fk.ref_table = expect_ident(l, err);
                    if (expect_token(l, TOK_LPAREN, err)) {
                        while (peek_token(l).type != TOK_RPAREN) {
                            char *c = expect_ident(l, err);
                            if (c) { fk.ref_columns = realloc(fk.ref_columns, (fk.ref_column_count+1)*sizeof(char*)); fk.ref_columns[fk.ref_column_count++] = c; }
                            if (peek_token(l).type == TOK_COMMA) next_token(l);
                        }
                        expect_token(l, TOK_RPAREN, err);
                    }
                    for (;;) {
                        if (match_ident_ci(l, "on")) {
                            if (match_ident_ci(l, "delete")) { Token a=next_token(l); if(a.type==TOK_IDENT){if(tolower(a.start[0])=='c')fk.on_delete=WL_FK_CASCADE;else if(tolower(a.start[0])=='r')fk.on_delete=WL_FK_RESTRICT;else if(tolower(a.start[0])=='s'&&a.length>4)fk.on_delete=WL_FK_SET_NULL;} }
                            else if (match_ident_ci(l, "update")) { Token a=next_token(l); if(a.type==TOK_IDENT){if(tolower(a.start[0])=='c')fk.on_update=WL_FK_CASCADE;else if(tolower(a.start[0])=='r')fk.on_update=WL_FK_RESTRICT;else if(tolower(a.start[0])=='s'&&a.length>4)fk.on_update=WL_FK_SET_NULL;} }
                        } else break;
                    }
                }
                table.foreign_keys = realloc(table.foreign_keys, (table.foreign_key_count+1)*sizeof(WlForeignKey));
                table.foreign_keys[table.foreign_key_count++] = fk;
            }
            continue;
        }

        /* Field declaration */
        if (match_ident_ci(l, "field")) {
            if (!parse_field(l, &table, err)) return 0;
        } else {
            next_token(l); /* skip unknown */
        }
    }

    if (!expect_token(l, TOK_RBRACE, err)) return 0;
    schema->tables = realloc(schema->tables, (schema->table_count+1)*sizeof(WlTable));
    schema->tables[schema->table_count++] = table;
    return 1;
}

static int parse_index(Lexer *l, WlSchema *schema, wlite_error **err) {
    WlIndex idx = {0};
    idx.name = expect_ident(l, err); if (!idx.name) return 0;
    if (!expect_token(l, TOK_LBRACE, err)) return 0;
    if (match_ident_ci(l, "unique")) idx.unique = 1;
    if (match_ident_ci(l, "on")) {
        idx.table = expect_ident(l, err);
        if (expect_token(l, TOK_LPAREN, err)) {
            while (peek_token(l).type != TOK_RPAREN) {
                if (peek_token(l).type == TOK_LPAREN) { next_token(l); idx.expression = consume_balanced(l); }
                else if (peek_token(l).type == TOK_IDENT) {
                    char *c = expect_ident(l, err);
                    if (c) { idx.columns = realloc(idx.columns, (idx.column_count+1)*sizeof(char*)); idx.columns[idx.column_count++] = c; }
                } else break;
                if (peek_token(l).type == TOK_COMMA) next_token(l);
            }
            expect_token(l, TOK_RPAREN, err);
        }
    }
    if (match_ident_ci(l, "where")) idx.where_clause = consume_expr(l);
    expect_token(l, TOK_RBRACE, err);
    schema->indexes = realloc(schema->indexes, (schema->index_count+1)*sizeof(WlIndex));
    schema->indexes[schema->index_count++] = idx;
    return 1;
}

static int parse_view(Lexer *l, WlSchema *schema, wlite_error **err) {
    WlView view = {0};
    view.name = expect_ident(l, err); if (!view.name) return 0;
    if (!expect_token(l, TOK_LBRACE, err)) return 0;
    if (match_ident_ci(l, "select")) {
        int depth = 0;
        const char *sql_start = l->current;
        while (*l->current) {
            if (*l->current == '(') depth++;
            else if (*l->current == ')') depth--;
            else if (*l->current == '}' && depth == 0) break;
            l->current++;
        }
        size_t sql_len = (size_t)(l->current - sql_start);
        view.sql = malloc(sql_len + 1); memcpy(view.sql, sql_start, sql_len); view.sql[sql_len] = '\0';
    }
    expect_token(l, TOK_RBRACE, err);
    schema->views = realloc(schema->views, (schema->view_count+1)*sizeof(WlView));
    schema->views[schema->view_count++] = view;
    return 1;
}

static int parse_trigger(Lexer *l, WlSchema *schema, wlite_error **err) {
    WlTrigger trig = {0};
    trig.name = expect_ident(l, err); if (!trig.name) return 0;
    if (!expect_token(l, TOK_LBRACE, err)) return 0;
    if (match_ident_ci(l, "before") || match_ident_ci(l, "after")) {}
    else if (match_ident_ci(l, "instead")) match_ident_ci(l, "of");
    if (match_ident_ci(l, "insert") || match_ident_ci(l, "delete")) {}
    else if (match_ident_ci(l, "update")) { match_ident_ci(l, "of"); while (peek_token(l).type == TOK_IDENT) next_token(l); }
    if (match_ident_ci(l, "on")) trig.table = expect_ident(l, err);
    if (match_ident_ci(l, "begin")) {
        int depth = 0;
        const char *body_start = l->current;
        while (*l->current) {
            if (*l->current == '{') depth++;
            else if (*l->current == '}') { if (depth == 0) break; depth--; }
            if (depth == 0 && *l->current == 'e' && strncmp(l->current, "end", 3) == 0 && !is_alnum(l->current[3])) { l->current += 3; break; }
            l->current++;
        }
        size_t body_len = (size_t)(l->current - body_start);
        while (body_len > 0 && (body_start[body_len-1] == ' ' || body_start[body_len-1] == '\n')) body_len--;
        trig.sql = malloc(body_len + 1); memcpy(trig.sql, body_start, body_len); trig.sql[body_len] = '\0';
    }
    expect_token(l, TOK_RBRACE, err);
    schema->triggers = realloc(schema->triggers, (schema->trigger_count+1)*sizeof(WlTrigger));
    schema->triggers[schema->trigger_count++] = trig;
    return 1;
}

static int parse_database(Lexer *l, WlSchema *schema, wlite_error **err) {
    (void)schema;
    if (!expect_token(l, TOK_LBRACE, err)) return 0;
    while (peek_token(l).type != TOK_RBRACE && peek_token(l).type != TOK_EOF) {
        if (peek_token(l).type == TOK_IDENT) { next_token(l);
            if (peek_token(l).type == TOK_IDENT && match_ident_ci(l, "=")) next_token(l);
        } else next_token(l);
    }
    expect_token(l, TOK_RBRACE, err);
    return 1;
}

static int parse_model_config(Lexer *l, WlSchema *schema, wlite_error **err) {
    (void)err;
    Token t = next_token(l);
    if (t.type == TOK_STRING) {
        free(schema->model_name);
        schema->model_name = token_to_string(t);
        if (schema->model_name[0] == '"' || schema->model_name[0] == '\'') {
            size_t len = strlen(schema->model_name);
            memmove(schema->model_name, schema->model_name + 1, len - 2);
            schema->model_name[len - 2] = '\0';
        }
    }
    Token v = peek_token(l);
    if (v.type == TOK_NUMBER) { next_token(l); schema->model_version = atoi(v.start); }
    return 1;
}

static int parse_top_level(Lexer *l, WlSchema *schema, wlite_error **err) {
    Token t = peek_token(l);
    if (t.type == TOK_EOF) return 1;
    if (t.type == TOK_IDENT) {
        if (match_ident_ci(l, "model")) return parse_model(l, schema, err);
        if (match_ident_ci(l, "model_config")) return parse_model_config(l, schema, err);
        if (match_ident_ci(l, "index")) return parse_index(l, schema, err);
        if (match_ident_ci(l, "view")) return parse_view(l, schema, err);
        if (match_ident_ci(l, "trigger")) return parse_trigger(l, schema, err);
        if (match_ident_ci(l, "database")) return parse_database(l, schema, err);
        expect_ident(l, err);
        if (peek_token(l).type == TOK_LBRACE) {
            next_token(l);
            int depth = 1;
            while (*l->current && depth > 0) {
                if (*l->current == '{') depth++;
                else if (*l->current == '}') depth--;
                l->current++;
            }
        }
        return 1;
    }
    next_token(l);
    return 1;
}

WlSchema *wl_schema_parse(const char *source, size_t length, wlite_error **error) {
    if (!source || length == 0) {
        if (error) { *error = calloc(1, sizeof(wlite_error)); (*error)->code = WLITE_ERR_NULL_PTR;
            (*error)->message = strdup("empty source"); }
        return NULL;
    }
    char *buf = malloc(length + 1); memcpy(buf, source, length); buf[length] = '\0';
    Lexer lexer; lexer_init(&lexer, buf);
    WlSchema *schema = calloc(1, sizeof(WlSchema));
    if (!schema) { free(buf); return NULL; }
    schema->version = 1;
    while (peek_token(&lexer).type != TOK_EOF) {
        if (!parse_top_level(&lexer, schema, error)) { wl_schema_free(schema); free(buf); return NULL; }
    }
    free(buf);
    return schema;
}

WlSchema *wl_schema_load(const char *path, wlite_error **error) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (error) { *error = calloc(1, sizeof(wlite_error)); (*error)->code = WLITE_ERR_IO;
            (*error)->message = strdup("cannot open file"); (*error)->object = strdup(path); }
        return NULL;
    }
    fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
    char *buf = malloc(size + 1); if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, size, f); buf[size] = '\0'; fclose(f);
    WlSchema *schema = wl_schema_parse(buf, size, error); free(buf);
    return schema;
}
