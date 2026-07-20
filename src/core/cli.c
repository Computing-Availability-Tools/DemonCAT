#include "cli.h"

#include <ctype.h>
#include <string.h>

typedef struct { const char *s; size_t i; size_t n; } cur_t;

static void skip_ws(cur_t *c) {
    while (c->i < c->n && isspace((unsigned char)c->s[c->i])) c->i++;
}
static int peek(cur_t *c) { return c->i < c->n ? (unsigned char)c->s[c->i] : -1; }

/* true if at current pos (after ws) input begins with literal word `lit` */
static int at_word(cur_t *c, const char *lit) {
    cur_t t = *c;
    skip_ws(&t);
    size_t n = strlen(lit);
    if (t.i + n > t.n) return 0;
    if (strncmp(t.s + t.i, lit, n) != 0) return 0;
    char after = (t.i + n < t.n) ? t.s[t.i + n] : '\0';
    if (after != '\0' && !isspace((unsigned char)after)) return 0;
    return 1;
}

static int parse_word(cur_t *c, char *out, size_t cap) {
    skip_ws(c);
    size_t k = 0;
    while (c->i < c->n && (isalnum((unsigned char)c->s[c->i]) || c->s[c->i] == '_')) {
        if (k + 1 < cap) out[k++] = c->s[c->i];
        c->i++;
    }
    out[k] = '\0';
    return k > 0 ? 0 : -1;
}

static int parse_key(cur_t *c, char *out, size_t cap) {
    skip_ws(c);
    size_t k = 0;
    while (c->i < c->n && (isalnum((unsigned char)c->s[c->i]) || c->s[c->i] == '_')) {
        if (k + 1 < cap) out[k++] = c->s[c->i];
        c->i++;
    }
    out[k] = '\0';
    return k > 0 ? 0 : -1;
}

static int parse_bare(cur_t *c, char *out, size_t cap) {
    skip_ws(c);
    size_t k = 0;
    while (c->i < c->n) {
        char ch = c->s[c->i];
        if (ch == ',' || ch == ')' || isspace((unsigned char)ch)) break;
        if (k + 1 < cap) out[k++] = ch;
        c->i++;
    }
    out[k] = '\0';
    return k > 0 ? 0 : -1;
}

/* form A: (k1,k2,...) values (v1,v2,...) */
static int parse_formA(cur_t *c, params_t *p) {
    skip_ws(c);
    if (peek(c) != '(') return -1;
    c->i++;
    char keys[DCAT_MAX_PARAMS][DCAT_KEY_LEN];
    char vals[DCAT_MAX_PARAMS][DCAT_VAL_LEN];
    int n = 0;
    for (;;) {
        if (n >= DCAT_MAX_PARAMS) return -1;
        if (parse_key(c, keys[n], sizeof keys[n])) return -1;
        n++;
        skip_ws(c);
        int ch = peek(c);
        if (ch == ',') { c->i++; continue; }
        if (ch == ')') { c->i++; break; }
        return -1;
    }
    skip_ws(c);
    char w[16];
    if (parse_word(c, w, sizeof w)) return -1;
    if (strcmp(w, "values")) return -1;
    skip_ws(c);
    if (peek(c) != '(') return -1;
    c->i++;
    int nv = 0;
    for (;;) {
        if (nv >= DCAT_MAX_PARAMS) return -1;
        if (parse_bare(c, vals[nv], sizeof vals[nv])) return -1;
        nv++;
        skip_ws(c);
        int ch = peek(c);
        if (ch == ',') { c->i++; continue; }
        if (ch == ')') { c->i++; break; }
        return -1;
    }
    if (nv != n) return -1;
    for (int i = 0; i < n; i++) {
        strcpy(p->items[i].key, keys[i]);
        strcpy(p->items[i].value, vals[i]);
    }
    p->count = n;
    return 0;
}

/* form B: where k1=v1 k2=v2 ... (caller consumes "where") */
static int parse_formB(cur_t *c, params_t *p) {
    for (;;) {
        skip_ws(c);
        if (peek(c) == -1) break;
        if (p->count >= DCAT_MAX_PARAMS) return -1;
        char k[DCAT_KEY_LEN];
        if (parse_key(c, k, sizeof k)) return -1;
        skip_ws(c);
        if (peek(c) != '=') return -1;
        c->i++;
        char v[DCAT_VAL_LEN];
        if (parse_bare(c, v, sizeof v)) return -1;
        strcpy(p->items[p->count].key, k);
        strcpy(p->items[p->count].value, v);
        p->count++;
    }
    return 0;
}

int cli_parse(const char *input, parsed_cmd_t *out) {
    if (!input || !out) return -1;
    cur_t c = {input, 0, strlen(input)};
    memset(out, 0, sizeof *out);

    char op[16];
    if (parse_word(&c, op, sizeof op)) return -1;
    if (!strcmp(op, "inject")) strcpy(out->op, "inject");
    else if (!strcmp(op, "clean")) strcpy(out->op, "clean");
    else if (!strcmp(op, "query")) strcpy(out->op, "query");
    else if (!strcmp(op, "list")) strcpy(out->op, "list");
    else return -1;

    if (!strcmp(out->op, "list")) return 0;

    skip_ws(&c);
    int is_where = at_word(&c, "where");
    int is_paren = (peek(&c) == '(');
    int is_end = (peek(&c) == -1);

    if (!strcmp(out->op, "inject") || !strcmp(out->op, "clean")) {
        if (is_where || is_paren || is_end) return -1;   /* uid required */
        if (parse_word(&c, out->uid, sizeof out->uid)) return -1;
    } else { /* query */
        if (is_paren) return -1;
        if (!(is_where || is_end)) {
            if (parse_word(&c, out->uid, sizeof out->uid)) return -1;
        }
    }

    skip_ws(&c);
    is_where = at_word(&c, "where");
    int is_paren2 = (peek(&c) == '(');
    int end2 = (peek(&c) == -1);

    if (!strcmp(out->op, "inject")) {
        if (is_paren2) { if (parse_formA(&c, &out->params)) return -1; }
        else if (end2) return 0;
        else return -1;
    } else { /* clean / query */
        if (is_where) {
            c.i += 5;                                   /* consume "where" */
            if (parse_formB(&c, &out->params)) return -1;
        } else if (end2) {
            return 0;
        } else return -1;
    }

    skip_ws(&c);
    if (peek(&c) != -1) return -1;                        /* trailing junk */
    return 0;
}
