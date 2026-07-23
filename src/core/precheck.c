/* src/core/precheck.c */
#include "precheck.h"
#include "executor.h"
#include "output.h"

#include <string.h>

static int csv_contains(const char *csv, const char *tok) {
    if (!csv || !tok) return 0;
    size_t tlen = strlen(tok);
    const char *p = csv;
    while (*p) {
        const char *c = strchr(p, ',');
        size_t len = c ? (size_t)(c - p) : strlen(p);
        if (len == tlen && !strncmp(p, tok, len)) return 1;
        if (!c) break;
        p = c + 1;
    }
    return 0;
}

static int param_is_declared(const char *key, const char *required, const char *optional) {
    return csv_contains(required, key) || csv_contains(optional, key);
}

static int find_param(const params_t *p, const char *key, const char **out_val) {
    for (int i = 0; i < p->count; i++) {
        if (!strcmp(p->items[i].key, key)) {
            if (out_val) *out_val = p->items[i].value;
            return 1;
        }
    }
    return 0;
}

result_t *precheck(const fault_def_t *f, const char *op, const params_t *p) {
    if (!f || !op) return result_err(op, "", DCAT_E_RUN, "bad args");

    /* Step 2: op ∈ supported_ops */
    if (!csv_contains(f->supported_ops, op))
        return result_err(op, f->uid, DCAT_E_PRECHECK, "op not supported");

    /* Step 5 (all commands): reject unknown params not in required/optional.
     * Checked before required-param completeness so an unrecognized key is
     * surfaced as "unknown param" even when a required key is also missing. */
    if (p) {
        for (int i = 0; i < p->count; i++) {
            if (!param_is_declared(p->items[i].key, f->required_params, f->optional_params))
                return result_err(op, f->uid, DCAT_E_PRECHECK, "unknown param");
        }
    }

    /* Step 3 (inject only): required_params present and non-empty */
    if (!strcmp(op, "inject") && f->required_params[0]) {
        const char *q = f->required_params;
        while (*q) {
            const char *c = strchr(q, ',');
            size_t len = c ? (size_t)(c - q) : strlen(q);
            char tok[64];
            if (len >= sizeof tok) len = sizeof tok - 1;
            memcpy(tok, q, len);
            tok[len] = '\0';
            const char *val = NULL;
            if (!find_param(p, tok, &val) || !val || !val[0])
                return result_err(op, f->uid, DCAT_E_PRECHECK, "missing required param");
            if (!c) break;
            q = c + 1;
        }
    }

    /* Step 4: script exists and executable */
    if (executor_check_tool(f->script) != 0)
        return result_err(op, f->uid, DCAT_E_PRECHECK, "script not executable");

    return result_ok(op, f->uid, NULL);
}
