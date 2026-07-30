#include "dispatch.h"
#include "registry.h"
#include "executor.h"
#include "precheck.h"
#include "reinject.h"
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

static result_t *cnf_inject(const fault_def_t *f, const params_t *params) {
    result_t *r = executor_run_fault(f, "inject", params, 0);
    if (r->code == 0 && !is_inject_only(f)) {
        long long id = state_add(f->uid, params);
        if (id < 0) {
            result_free(r);
            return result_err("inject", f->uid, 1,
                "state table full, cannot track injection — run 'dcat query' and clean existing faults");
        }
        cJSON *root = cJSON_Parse(r->json);
        if (root) {
            cJSON *data = cJSON_GetObjectItem(root, "data");
            if (data) cJSON_AddNumberToObject(data, "record_id", (double)id);
            char *s = cJSON_PrintUnformatted(root);
            cJSON_Delete(root);
            free(r->json);
            r->json = s;
        }
    }
    return r;
}

/* 清单条活动记录(复用于 cnf_clean 与 --force 替换路径)。
 * 成功返回 NULL; 失败返回 executor 的 err(调用方 result_free)。 */
static result_t *clean_one_record(const fault_def_t *f, long long record_id) {
    const injection_record_t *rec = state_find_by_id(record_id);
    if (!rec) return NULL;
    result_t *r = executor_run_fault(f, "clean", &rec->params, 0);
    if (r->code != 0) return r;
    state_mark_inactive(record_id);
    result_free(r);
    return NULL;
}

/* 格式化记录的全部参数为 "key=val,key=val"（用于 reinject 拒绝消息展示前次注入）。 */
static void fmt_record_params(const injection_record_t *rec, char *out, size_t cap) {
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (!rec) return;
    size_t len = 0;
    for (int i = 0; i < rec->params.count && len + 1 < cap; i++) {
        int w = snprintf(out + len, cap - len, "%s%s=%s",
                         len ? "," : "", rec->params.items[i].key, rec->params.items[i].value);
        if (w < 0) break;
        if ((size_t)w >= cap - len) { out[cap - 1] = '\0'; break; }
        len += (size_t)w;
    }
}

static result_t *cnf_clean(const fault_def_t *f, const params_t *user_params) {
    /* clean <uid> 无参 = clean-all-for-uid：脚本自行 glob /tmp 工件，绕过 state */
    if (user_params->count == 0)
        return executor_run_fault(f, "clean", user_params, 0);
    long long ids[DCAT_MAX_RECORDS];
    int n = state_find_by_params(f->uid, user_params, ids, DCAT_MAX_RECORDS);
    if (n == 0) {
        /* state 丢失/损坏时回退：用用户参数直接调脚本 clean（脚本读 /tmp 工件清理） */
        if (state_is_lost())
            return executor_run_fault(f, "clean", user_params, 0);
        return result_err("clean", f->uid, 1, "no active injection");
    }
    for (int i = 0; i < n; i++) {
        result_t *r = clean_one_record(f, ids[i]);
        if (r) return r;
    }
    return result_ok("clean", f->uid, 0, "cleaned");
}

/* clean --all：遍历全部注册故障(cnf)，对支持 clean 的逐个执行无参 clean；
 * stateless，不依赖 state.json（脚本自行 glob /tmp 工件）。聚合每 uid 结果。 */
result_t *dispatch_clean_all(void) {
    cJSON *arr = cJSON_CreateArray();
    params_t empty; params_init(&empty);
    int n = 0; const fault_def_t *list = registry_list(&n);
    for (int i = 0; i < n; i++) {
        if (!op_in_supported(list[i].supported_ops, "clean")) continue;
        result_t *r = executor_run_fault(&list[i], "clean", &empty, 0);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "uid", list[i].uid);
        cJSON_AddStringToObject(o, "status", (r && r->code == 0) ? "ok" : "error");
        cJSON_AddItemToArray(arr, o);
        if (r) result_free(r);
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddStringToObject(root, "op", "clean");
    cJSON_AddStringToObject(root, "mode", "all");
    cJSON_AddItemToObject(root, "data", arr);
    char *s = cJSON_PrintUnformatted(root); cJSON_Delete(root);
    result_t *r = malloc(sizeof(result_t)); r->code = 0; r->json = s; return r;
}

/* 第3层：动态插件 dispatch（通用预检 + plugin->precheck + 函数指针 + state） */
static result_t *plugin_dispatch(const dcat_plugin_t *p, const char *op, const params_t *params) {
    if (!op_in_supported(p->supported_ops, op))
        return result_err(op, p->uid, 3, "op not in supported_ops");
    if (!declared_params_only(p->inject_required, p->inject_optional, p->clean_required, p->clean_optional, p->query_required, p->query_optional, params)) {
        char msg[256];
        snprintf(msg, sizeof msg, "unknown parameter '%s' (not declared for %s)", precheck_last_undeclared_param(), p->uid);
        return result_err(op, p->uid, 3, msg);
    }
    const char *op_req = NULL;
    if (strcmp(op, "inject") == 0)     op_req = p->inject_required;
    else if (strcmp(op, "clean") == 0) op_req = p->clean_required;
    /* query 不强制必填参数（无参时脚本自行展示全部），与 cnf 路径 precheck 一致 */
    if (op_req && !required_params_present(op_req, params))
        return result_err(op, p->uid, 3, "missing required params");
    if (p->precheck) {
        result_t *pc = p->precheck(op, params);
        if (pc && pc->code != 0) return pc;
        if (pc) result_free(pc);
    }
    if (strcmp(op, "inject") == 0) {
        if (!p->inject) return result_err("inject", p->uid, 3, "inject declared in supported_ops but inject() not implemented");
        result_t *r = p->inject(params);
        if (r->code == 0 && p->clean) {
            long long id = state_add(p->uid, params);
            if (id < 0) {
                result_free(r);
                return result_err("inject", p->uid, 1,
                    "state table full, cannot track injection — run 'dcat query' and clean existing faults");
            }
            cJSON *root = cJSON_Parse(r->json);
            if (root) {
                cJSON *data = cJSON_GetObjectItem(root, "data");
                if (data) cJSON_AddNumberToObject(data, "record_id", (double)id);
                char *s = cJSON_PrintUnformatted(root);
                cJSON_Delete(root);
                free(r->json);
                r->json = s;
            }
        }
        return r;
    }
    if (strcmp(op, "clean") == 0) {
        if (!p->clean) return result_err("clean", p->uid, 3, "clean declared in supported_ops but clean() not implemented");
        long long ids[DCAT_MAX_RECORDS];
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
        if (!p->query) return result_err("query", p->uid, 3, "query declared in supported_ops but query() not implemented");
        return p->query(params);
    }
    return result_err(op, p->uid, 3, "op not in supported_ops");
}

/* query 无 uid：列出 dcat 自身全部活跃注入记录（SPEC §6） */
static void append_active_record(const injection_record_t *r, void *ctx) {
    cJSON *arr = (cJSON *)ctx;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "uid", r->uid);
    cJSON_AddNumberToObject(o, "record_id", (double)r->record_id);
    cJSON_AddStringToObject(o, "started_at", r->started_at);
    cJSON_AddBoolToObject(o, "active", r->active);
    cJSON *p = cJSON_CreateObject();
    for (int i = 0; i < r->params.count; i++)
        cJSON_AddStringToObject(p, r->params.items[i].key, r->params.items[i].value);
    cJSON_AddItemToObject(o, "params", p);
    cJSON_AddItemToArray(arr, o);
}

static result_t *dispatch_query_all(void) {
    cJSON *arr = cJSON_CreateArray();
    state_for_each_active(append_active_record, arr);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddStringToObject(root, "op", "query");
    cJSON_AddItemToObject(root, "data", arr);
    char *s = cJSON_PrintUnformatted(root); cJSON_Delete(root);
    result_t *r = malloc(sizeof(result_t)); r->code = 0; r->json = s; return r;
}

result_t *dispatch_route_force(const char *uid, const char *op, const params_t *params, int force) {
    if (strcmp(op, "list") == 0) return dispatch_list();
    if (strcmp(op, "query") == 0 && (uid == NULL || uid[0] == '\0'))
        return dispatch_query_all();
    if (uid == NULL || uid[0] == '\0')
        return result_err(op, "", 2, "uid required (use 'dcat list' to see available faults)");

    const fault_def_t *f = registry_find(uid);
    if (f) {
        result_t *pc = precheck(f, op, params);
        if (pc) return pc;
        if (strcmp(op, "inject") == 0) {
            long long ids[DCAT_MAX_RECORDS];
            int on = reinject_find_overlap(f, params, ids, DCAT_MAX_RECORDS);
            if (on > 0 && !force) {
                char msg[512];
                int off = 0;
                int w = snprintf(msg, sizeof msg, "resource already injected");
                if (w < 0) off = (int)sizeof msg; else off = w;
                int shown = on < 3 ? on : 3;
                for (int k = 0; k < shown && off < (int)sizeof msg; k++) {
                    const injection_record_t *rec = state_find_by_id(ids[k]);
                    char pstr[256]; pstr[0] = '\0';
                    if (rec) fmt_record_params(rec, pstr, sizeof pstr);
                    const char *sep = (k == 0) ? " (" : "; ";
                    w = snprintf(msg + off, sizeof msg - (size_t)off,
                                 "%srecord id %lld: %s", sep, ids[k], pstr);
                    if (w < 0) { off = (int)sizeof msg; break; }
                    off += w;
                }
                if (on > 3 && off < (int)sizeof msg) {
                    w = snprintf(msg + off, sizeof msg - (size_t)off, "; +%d more", on - 3);
                    if (w < 0) off = (int)sizeof msg; else off += w;
                }
                if (off < (int)sizeof msg)
                    snprintf(msg + off, sizeof msg - (size_t)off, "); use --force to replace");
                return result_err("inject", f->uid, DCAT_REINJECT_CONFLICT, msg);
            }
            if (on > 0 && force) {
                for (int k = 0; k < on; k++) {
                    result_t *rc = clean_one_record(f, ids[k]);
                    if (rc) {
                        char m[320]; const char *detail = "";
                        cJSON *root = cJSON_Parse(rc->json);
                        if (root) {
                            cJSON *e = cJSON_GetObjectItem(root, "error");
                            cJSON *mj = e ? cJSON_GetObjectItem(e, "message") : NULL;
                            if (mj && cJSON_IsString(mj)) detail = mj->valuestring;
                        }
                        snprintf(m, sizeof m, "--force: clean of record %lld failed: %s", ids[k], detail);
                        cJSON_Delete(root);
                        result_free(rc);
                        return result_err("inject", f->uid, 1, m);
                    }
                }
            }
            return cnf_inject(f, params);
        }
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
            if (r->code == 0 && inj->clean) {
                long long id = state_add(uid, params);
                if (id < 0) {
                    result_free(r);
                    return result_err("inject", uid, 1,
                        "state table full, cannot track injection — run 'dcat query' and clean existing faults");
                }
            }
            return r;
        }
        if (strcmp(op, "clean") == 0) {
            long long ids[DCAT_MAX_RECORDS];
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
    char msg[256];
    snprintf(msg, sizeof msg, "uid '%s' not found in catalog (use 'dcat list' to see available faults)", uid ? uid : "");
    return result_err(op, uid ? uid : "", 4, msg);
}

result_t *dispatch_route(const char *uid, const char *op, const params_t *params) {
    return dispatch_route_force(uid, op, params, 0);
}
