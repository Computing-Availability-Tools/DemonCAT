/* src/core/dispatch.c */
#include "dispatch.h"
#include "registry.h"
#include "injectors/injector.h"
#include "precheck.h"
#include "executor.h"
#include "state.h"
#include "output.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int is_inject_only(const fault_def_t *f) {
    const char *p = f->supported_ops;
    while (*p) {
        const char *c = strchr(p, ',');
        size_t len = c ? (size_t)(c - p) : strlen(p);
        if (len == 5 && !strncmp(p, "clean", 5)) return 0;
        if (!c) break;
        p = c + 1;
    }
    return 1;
}

static cJSON *ops_array(const char *csv) {
    cJSON *a = cJSON_CreateArray();
    if (!csv) return a;
    const char *p = csv;
    while (*p) {
        const char *c = strchr(p, ',');
        size_t len = c ? (size_t)(c - p) : strlen(p);
        char tok[32];
        if (len >= sizeof tok) len = sizeof tok - 1;
        memcpy(tok, p, len);
        tok[len] = '\0';
        cJSON_AddItemToArray(a, cJSON_CreateString(tok));
        if (!c) break;
        p = c + 1;
    }
    return a;
}

static void extract_message(const result_t *rr, cJSON *data) {
    if (!rr || !data) return;
    cJSON *root = cJSON_Parse(rr->json);
    if (root) {
        cJSON *d = cJSON_GetObjectItem(root, "data");
        cJSON *msg = d ? cJSON_GetObjectItem(d, "message") : NULL;
        if (msg && msg->valuestring && msg->valuestring[0])
            cJSON_AddStringToObject(data, "message", msg->valuestring);
        cJSON_Delete(root);
    }
}

result_t *dispatch_list(void) {
    int n;
    const fault_def_t *t = registry_list(&n);
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < n; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "uid", t[i].uid);
        cJSON_AddStringToObject(o, "module", t[i].module);
        cJSON_AddItemToObject(o, "supported_ops", ops_array(t[i].supported_ops));
        if (t[i].desc[0]) cJSON_AddStringToObject(o, "desc", t[i].desc);
        if (t[i].required_params[0])
            cJSON_AddItemToObject(o, "required_params", ops_array(t[i].required_params));
        if (t[i].optional_params[0])
            cJSON_AddItemToObject(o, "optional_params", ops_array(t[i].optional_params));
        cJSON_AddItemToArray(arr, o);
    }
    return result_ok("list", NULL, arr);
}

result_t *dispatch_query(const char *uid, const params_t *p) {
    if (!uid || !uid[0]) {
        cJSON *arr = cJSON_CreateArray();
        for (int i = 0; i < DCAT_MAX_RECORDS; i++) {
            injection_record_t r;
            if (!state_record(i, &r)) continue;
            cJSON *o = cJSON_CreateObject();
            cJSON_AddNumberToObject(o, "record_id", r.record_id);
            cJSON_AddStringToObject(o, "uid", r.uid);
            cJSON_AddNumberToObject(o, "started_at", (double)r.started_at);
            cJSON_AddBoolToObject(o, "active", r.active);
            cJSON *pobj = cJSON_CreateObject();
            for (int j = 0; j < r.params.count; j++)
                cJSON_AddStringToObject(pobj, r.params.items[j].key, r.params.items[j].value);
            cJSON_AddItemToObject(o, "params", pobj);
            cJSON_AddItemToArray(arr, o);
        }
        return result_ok("query", NULL, arr);
    }

    const fault_def_t *f = registry_find(uid);
    if (!f) return result_err("query", uid, DCAT_E_NOTFOUND, "uid not found");

    result_t *pc = precheck(f, "query", p);
    if (pc->code != 0) return pc;
    result_free(pc);

    char cmd[256];
    executor_build_cmd(f, "query", p, cmd, sizeof cmd);
    executor_set_env("query", uid, p);
    int rc = executor_run_raw(cmd);

    printf("---\n");
    fflush(stdout);

    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "confirmed", rc == 0);
    return result_ok("query", uid, data);
}

result_t *dispatch_inject(const char *uid, const params_t *p) {
    const fault_def_t *f = registry_find(uid);
    if (!f) return result_err("inject", uid, DCAT_E_NOTFOUND, "uid not found");

    result_t *pc = precheck(f, "inject", p);
    if (pc->code != 0) return pc;
    result_free(pc);

    char cmd[256];
    executor_build_cmd(f, "inject", p, cmd, sizeof cmd);
    executor_set_env("inject", f->uid, p);

    result_t *rr = executor_run(cmd);
    if (!rr || rr->code != 0) {
        result_t *err = result_err("inject", f->uid, DCAT_E_RUN,
                                  rr && rr->json ? "script failed" : "internal error");
        result_free(rr);
        return err;
    }

    cJSON *data = cJSON_CreateObject();
    extract_message(rr, data);
    result_free(rr);

    if (is_inject_only(f)) {
        return result_ok("inject", f->uid, data);
    }

    int id = state_add(f->uid, p);
    cJSON_AddNumberToObject(data, "record_id", id);
    return result_ok("inject", f->uid, data);
}

result_t *dispatch_clean(const char *uid, const params_t *p) {
    const fault_def_t *f = registry_find(uid);
    if (!f) return result_err("clean", uid, DCAT_E_NOTFOUND, "uid not found");

    result_t *pc = precheck(f, "clean", p);
    if (pc->code != 0) return pc;
    result_free(pc);

    injection_record_t matches[DCAT_MAX_RECORDS];
    int n = state_find_by_params(uid, p, matches, DCAT_MAX_RECORDS);
    if (n == 0) return result_err("clean", uid, DCAT_E_RUN, "no active injection");

    char cmd[256];
    int cleaned = 0;
    for (int i = 0; i < n; i++) {
        executor_build_cmd(f, "clean", &matches[i].params, cmd, sizeof cmd);
        executor_set_env("clean", f->uid, &matches[i].params);
        result_t *rr = executor_run(cmd);
        if (!rr || rr->code != 0) {
            result_t *err = result_err("clean", f->uid, DCAT_E_RUN, "clean script failed");
            result_free(rr);
            if (cleaned > 0) state_save();
            return err;
        }
        result_free(rr);
        state_mark_inactive(matches[i].record_id);
        cleaned++;
    }

    cJSON *data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "cleaned", cleaned);
    return result_ok("clean", f->uid, data);
}
