#include "precheck.h"
#include "executor.h"
#include "output.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static char g_undeclared_param[64];

int op_in_supported(const char *supported_ops, const char *op) {
    char buf[128];
    strncpy(buf, supported_ops ? supported_ops : "", sizeof(buf)-1);
    buf[sizeof(buf)-1] = '\0';
    char *save = NULL;
    char *tok = strtok_r(buf, ",", &save);
    while (tok) { if (strcmp(tok, op) == 0) return 1; tok = strtok_r(NULL, ",", &save); }
    return 0;
}

int required_params_present(const char *required_params, const params_t *params) {
    char buf[128];
    strncpy(buf, required_params ? required_params : "", sizeof(buf)-1);
    buf[sizeof(buf)-1] = '\0';
    if (buf[0] == '\0') return 1;
    char *save = NULL;
    char *tok = strtok_r(buf, ",", &save);
    while (tok) {
        const char *v = params_find(params, tok);
        if (!v || !v[0]) return 0;
        tok = strtok_r(NULL, ",", &save);
    }
    return 1;
}

int declared_params_only(const char *inject_req, const char *inject_opt, const char *clean_req, const char *clean_opt, const char *query_req, const char *query_opt, const params_t *params) {
    /* returns 1 if all params are declared, 0 otherwise. Sets g_undeclared_param to the first undeclared param. */
    for (int i = 0; i < params->count; i++) {
        const char *k = params->items[i].key;
        const char *lists[6] = { inject_req, inject_opt, clean_req, clean_opt, query_req, query_opt };
        int found = 0;
        for (int li = 0; li < 6 && !found; li++) {
            if (!lists[li] || !lists[li][0]) continue;
            char buf[128];
            strncpy(buf, lists[li], sizeof(buf)-1); buf[sizeof(buf)-1]='\0';
            char *save = NULL;
            char *tok = strtok_r(buf, ",", &save);
            while (tok) { if (strcmp(tok, k) == 0) { found = 1; break; } tok = strtok_r(NULL, ",", &save); }
        }
        if (!found) {
            strncpy(g_undeclared_param, k, sizeof(g_undeclared_param)-1);
            g_undeclared_param[sizeof(g_undeclared_param)-1] = '\0';
            return 0;
        }
    }
    return 1;
}

const char *precheck_last_undeclared_param(void) { return g_undeclared_param; }

/* Find first missing required param. Returns static string or NULL. */
static const char *first_missing_required(const char *required, const params_t *params) {
    static char missing[64];
    if (!required || !required[0]) return NULL;
    char buf[128];
    strncpy(buf, required, sizeof(buf)-1); buf[sizeof(buf)-1]='\0';
    char *save = NULL;
    char *tok = strtok_r(buf, ",", &save);
    while (tok) {
        const char *v = params_find(params, tok);
        if (!v || !v[0]) { strncpy(missing, tok, sizeof(missing)-1); missing[sizeof(missing)-1]='\0'; return missing; }
        tok = strtok_r(NULL, ",", &save);
    }
    return NULL;
}

result_t *precheck(const fault_def_t *f, const char *op, const params_t *params) {
    if (!f) return result_err(op, "", 4, "uid not found (use 'dcat list' to see available faults)");
    if (!op_in_supported(f->supported_ops, op))
        return result_err(op, f->uid, 3, "op not in supported_ops");
    if (!declared_params_only(f->inject_required, f->inject_optional, f->clean_required, f->clean_optional, f->query_required, f->query_optional, params)) {
        char msg[256];
        snprintf(msg, sizeof msg, "unknown parameter '%s' (not declared for %s)", g_undeclared_param, f->uid);
        return result_err(op, f->uid, 3, msg);
    }
    const char *op_required = NULL;
    if (strcmp(op, "inject") == 0)      op_required = f->inject_required;
    else if (strcmp(op, "clean") == 0)  op_required = f->clean_required;
    /* query 不强制必填参数：无参时脚本自行展示全部（如全核/全网卡），有参则按参过滤 */
    if (op_required) {
        const char *missing = first_missing_required(op_required, params);
        if (missing) {
            char msg[256];
            snprintf(msg, sizeof msg, "missing required parameter '%s' for %s", missing, op);
            return result_err(op, f->uid, 3, msg);
        }
    }
    if (executor_check_tool(f->script) != 0)
        return result_err(op, f->uid, 3, "script not executable");
    return NULL;
}
