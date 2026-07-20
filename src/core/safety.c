#include "safety.h"
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

int safety_confirm(safety_level_t level, const char *answer) {
    switch (level) {
        case SAFETY_NORMAL:    return 1;
        case SAFETY_WARNING:   return answer && (answer[0] == 'y' || answer[0] == 'Y');
        case SAFETY_DANGEROUS: return answer && strcmp(answer, "yes") == 0;
        default:               return 0;
    }
}

result_t *safety_precheck(const fault_def_t *f, const char *op, const params_t *p) {
    if (!f || !op) return result_err(op, "", DCAT_E_RUN, "bad args");
    if (!csv_contains(f->supported_ops, op))
        return result_err(op, f->uid, DCAT_E_SAFETY, "op not supported");
    if (!strcmp(op, "inject") && f->required_params[0]) {
        const char *q = f->required_params;
        while (*q) {
            const char *c = strchr(q, ',');
            size_t len = c ? (size_t)(c - q) : strlen(q);
            char tok[64];
            if (len >= sizeof tok) len = sizeof tok - 1;
            memcpy(tok, q, len);
            tok[len] = '\0';
            int found = 0;
            for (int i = 0; i < p->count; i++) {
                if (!strcmp(p->items[i].key, tok) && p->items[i].value[0]) { found = 1; break; }
            }
            if (!found) return result_err(op, f->uid, DCAT_E_SAFETY, "missing required param");
            if (!c) break;
            q = c + 1;
        }
    }
    if (executor_check_tool(f->script) != 0)
        return result_err(op, f->uid, DCAT_E_SAFETY, "script not executable");
    return result_ok(op, f->uid, NULL);
}
