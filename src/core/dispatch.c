#include "dispatch.h"
#include "registry.h"
#include "executor.h"
#include "precheck.h"
#include "state.h"
#include "output.h"
#include "config.h"
#include "../injectors/injector.h"
#include "../plugins/plugin_manager.h"
#include <cJSON.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int is_inject_only(const fault_def_t *f) {
    return strcmp(f->supported_ops, "inject") == 0;
}

static result_t *dispatch_list(void) {
    int n = 0; const fault_def_t *list = registry_list(&n);
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < n; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "uid", list[i].uid);
        cJSON_AddStringToObject(o, "module", list[i].module);
        cJSON *ops = cJSON_CreateArray();
        char buf[64]; strncpy(buf, list[i].supported_ops, sizeof(buf)-1); buf[sizeof(buf)-1]='\0';
        char *save = NULL, *tok = strtok_r(buf, ",", &save);
        while (tok) { cJSON_AddItemToArray(ops, cJSON_CreateString(tok)); tok = strtok_r(NULL, ",", &save); }
        cJSON_AddItemToObject(o, "supported_ops", ops);
        if (list[i].desc[0]) cJSON_AddStringToObject(o, "desc", list[i].desc);
        cJSON_AddItemToArray(arr, o);
    }
    /* 动态插件纳入 list */
    int pc = 0;
    const dcat_plugin_t *const *plugs = plugin_list(&pc);
    for (int i = 0; i < pc; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "uid", plugs[i]->uid);
        cJSON_AddStringToObject(o, "module", plugs[i]->name ? plugs[i]->name : "");
        cJSON *ops = cJSON_CreateArray();
        char pbuf[64]; strncpy(pbuf, plugs[i]->supported_ops, sizeof(pbuf)-1); pbuf[sizeof(pbuf)-1]='\0';
        char *psave = NULL, *ptok = strtok_r(pbuf, ",", &psave);
        while (ptok) { cJSON_AddItemToArray(ops, cJSON_CreateString(ptok)); ptok = strtok_r(NULL, ",", &psave); }
        cJSON_AddItemToObject(o, "supported_ops", ops);
        if (plugs[i]->description && plugs[i]->description[0]) cJSON_AddStringToObject(o, "desc", plugs[i]->description);
        cJSON_AddItemToArray(arr, o);
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddStringToObject(root, "op", "list");
    cJSON_AddItemToObject(root, "data", arr);
    char *s = cJSON_PrintUnformatted(root); cJSON_Delete(root);
    result_t *r = malloc(sizeof(result_t)); r->code = 0; r->json = s; return r;
}

struct list_ctx { cJSON *arr; };
static void push_record(const injection_record_t *r, void *v) {
    struct list_ctx *c = v;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "uid", r->uid);
    cJSON_AddNumberToObject(o, "record_id", r->record_id);
    cJSON_AddNumberToObject(o, "started_at", r->started_at);
    cJSON_AddBoolToObject(o, "active", r->active);
    cJSON *prms = cJSON_CreateObject();
    for (int k = 0; k < r->params.count; k++)
        cJSON_AddStringToObject(prms, r->params.items[k].key, r->params.items[k].value);
    cJSON_AddItemToObject(o, "params", prms);
    cJSON_AddItemToArray(c->arr, o);
}
static result_t *dispatch_query_state(void) {
    cJSON *arr = cJSON_CreateArray();
    struct list_ctx c = { arr };
    state_for_each_active(push_record, &c);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddStringToObject(root, "op", "query");
    cJSON_AddItemToObject(root, "data", arr);
    char *s = cJSON_PrintUnformatted(root); cJSON_Delete(root);
    result_t *r = malloc(sizeof(result_t)); r->code = 0; r->json = s; return r;
}

static result_t *cnf_inject(const fault_def_t *f, const params_t *params) {
    result_t *r = executor_run_fault(f, "inject", params, 0);
    if (r->code == 0 && !is_inject_only(f)) {
        int id = state_add(f->uid, params);
        cJSON *root = cJSON_Parse(r->json);
        if (root) {
            cJSON *data = cJSON_GetObjectItem(root, "data");
            if (data) cJSON_AddNumberToObject(data, "record_id", id);
            char *s = cJSON_PrintUnformatted(root);
            cJSON_Delete(root);
            free(r->json);
            r->json = s;
        }
    }
    return r;
}

static result_t *cnf_clean(const fault_def_t *f, const params_t *user_params) {
    int ids[DCAT_MAX_RECORDS];
    int n = state_find_by_params(f->uid, user_params, ids, DCAT_MAX_RECORDS);
    if (n == 0) return result_err("clean", f->uid, 1, "no active injection");
    for (int i = 0; i < n; i++) {
        const injection_record_t *rec = state_find_by_id(ids[i]);
        if (!rec) continue;
        result_t *r = executor_run_fault(f, "clean", &rec->params, 0);
        if (r->code != 0) return r;
        state_mark_inactive(ids[i]);
        result_free(r);
    }
    return result_ok("clean", f->uid, 0, "cleaned");
}

/* 第3层：动态插件 dispatch（通用预检 + plugin->precheck + 函数指针 + state） */
static result_t *plugin_dispatch(const dcat_plugin_t *p, const char *op, const params_t *params) {
    if (!op_in_supported(p->supported_ops, op))
        return result_err(op, p->uid, 3, "op not in supported_ops");
    if (!declared_params_only(p->required_params, p->optional_params, params))
        return result_err(op, p->uid, 3, "undeclared param");
    if (strcmp(op, "inject") == 0 && !required_params_present(p->required_params, params))
        return result_err(op, p->uid, 3, "missing required params");
    if (p->precheck) {
        result_t *pc = p->precheck(op, params);
        if (pc && pc->code != 0) return pc;
        if (pc) result_free(pc);
    }
    if (strcmp(op, "inject") == 0) {
        result_t *r = p->inject(params);
        if (r->code == 0 && p->clean) {
            int id = state_add(p->uid, params);
            cJSON *root = cJSON_Parse(r->json);
            if (root) {
                cJSON *data = cJSON_GetObjectItem(root, "data");
                if (data) cJSON_AddNumberToObject(data, "record_id", id);
                char *s = cJSON_PrintUnformatted(root);
                cJSON_Delete(root);
                free(r->json);
                r->json = s;
            }
        }
        return r;
    }
    if (strcmp(op, "clean") == 0) {
        if (!p->clean) return result_err("clean", p->uid, 3, "op not in supported_ops");
        int ids[DCAT_MAX_RECORDS];
        int n = state_find_by_params(p->uid, params, ids, DCAT_MAX_RECORDS);
        if (n == 0) return result_err("clean", p->uid, 1, "no active injection");
        for (int i = 0; i < n; i++) {
            const injection_record_t *rec = state_find_by_id(ids[i]);
            if (!rec) continue;
            result_t *r = p->clean(&rec->params);
            if (r->code != 0) return r;
            state_mark_inactive(ids[i]);
            result_free(r);
        }
        return result_ok("clean", p->uid, 0, "cleaned");
    }
    if (strcmp(op, "query") == 0) {
        if (!p->query) return result_err("query", p->uid, 3, "op not in supported_ops");
        return p->query(params);
    }
    return result_err(op, p->uid, 3, "op not in supported_ops");
}

result_t *dispatch_route(const char *uid, const char *op, const params_t *params) {
    if (strcmp(op, "list") == 0) return dispatch_list();
    if (strcmp(op, "query") == 0 && (uid == NULL || uid[0] == '\0'))
        return dispatch_query_state();

    const fault_def_t *f = registry_find(uid);
    if (f) {
        result_t *pc = precheck(f, op, params);
        if (pc) return pc;
        if (strcmp(op, "inject") == 0) return cnf_inject(f, params);
        if (strcmp(op, "clean") == 0)   return cnf_clean(f, params);
        if (strcmp(op, "query") == 0) {
            int rc = executor_run_raw_fault(f, "query", params);
            printf("---\n");
            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "status", "ok");
            cJSON_AddStringToObject(root, "op", "query");
            cJSON_AddStringToObject(root, "uid", uid);
            cJSON *data = cJSON_AddObjectToObject(root, "data");
            cJSON_AddBoolToObject(data, "confirmed", rc == 0);
            char *s = cJSON_PrintUnformatted(root); cJSON_Delete(root);
            result_t *r = malloc(sizeof(result_t)); r->code = 0; r->json = s; return r;
        }
    }
    const injector_t *inj = injector_find(uid);
    if (inj) {
        result_t *pc = inj->precheck(op, params);
        if (pc && pc->code != 0) return pc;
        if (pc) result_free(pc);
        if (strcmp(op, "inject") == 0) {
            result_t *r = inj->inject(params);
            if (r->code == 0 && inj->clean) state_add(uid, params);
            return r;
        }
        if (strcmp(op, "clean") == 0) {
            int ids[DCAT_MAX_RECORDS];
            int n = state_find_by_params(uid, params, ids, DCAT_MAX_RECORDS);
            if (n == 0) return result_err("clean", uid, 1, "no active injection");
            for (int i = 0; i < n; i++) {
                const injection_record_t *rec = state_find_by_id(ids[i]);
                if (!rec) continue;
                result_t *r = inj->clean(&rec->params);
                if (r->code != 0) return r;
                state_mark_inactive(ids[i]); result_free(r);
            }
            return result_ok("clean", uid, 0, "cleaned");
        }
        if (strcmp(op, "query") == 0) return inj->query(params);
    }
    const dcat_plugin_t *plg = plugin_find(uid);
    if (plg) return plugin_dispatch(plg, op, params);
    return result_err(op, uid ? uid : "", 4, "not found");
}
