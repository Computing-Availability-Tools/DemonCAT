#include "precheck.h"
#include "executor.h"
#include "output.h"
#include <string.h>
#include <stdlib.h>

int op_in_supported(const char *supported_ops, const char *op) {
    char buf[128];
    strncpy(buf, supported_ops ? supported_ops : "", sizeof(buf)-1);
    buf[sizeof(buf)-1] = '\0';
    char *tok = strtok(buf, ",");
    while (tok) { if (strcmp(tok, op) == 0) return 1; tok = strtok(NULL, ","); }
    return 0;
}

int required_params_present(const fault_def_t *f, const params_t *params) {
    char buf[128];
    strncpy(buf, f->required_params, sizeof(buf)-1);
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

int declared_params_only(const fault_def_t *f, const params_t *params) {
    for (int i = 0; i < params->count; i++) {
        const char *k = params->items[i].key;
        char req[128], opt[128];
        strncpy(req, f->required_params, sizeof(req)-1); req[sizeof(req)-1]='\0';
        strncpy(opt, f->optional_params, sizeof(opt)-1); opt[sizeof(opt)-1]='\0';
        int found = 0;
        char *save = NULL;
        char *tok = strtok_r(req, ",", &save);
        while (tok) { if (strcmp(tok, k) == 0) { found = 1; break; } tok = strtok_r(NULL, ",", &save); }
        if (!found && opt[0] != '\0') {
            save = NULL;
            tok = strtok_r(opt, ",", &save);
            while (tok) { if (strcmp(tok, k) == 0) { found = 1; break; } tok = strtok_r(NULL, ",", &save); }
        }
        if (!found) return 0;
    }
    return 1;
}

result_t *precheck(const fault_def_t *f, const char *op, const params_t *params) {
    if (!f) return result_err(op, "", 4, "uid not found");
    if (!op_in_supported(f->supported_ops, op))
        return result_err(op, f->uid, 3, "op not in supported_ops");
    if (!declared_params_only(f, params))
        return result_err(op, f->uid, 3, "undeclared param");
    if (strcmp(op, "inject") == 0 && !required_params_present(f, params))
        return result_err(op, f->uid, 3, "missing required params");
    if (executor_check_tool(f->script) != 0)
        return result_err(op, f->uid, 3, "script not executable");
    return NULL;
}
