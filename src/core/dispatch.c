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

/* 动态列表行：uid/module/ops/desc。收集后排版为文本表格，而非裸 JSON。 */
typedef struct {
    const char *uid;
    const char *module;
    const char *ops;
    const char *desc;
} list_row_t;
#define MAX_LIST_ROWS 128

static result_t *dispatch_list(void) {
    list_row_t rows[MAX_LIST_ROWS];
    char opsbuf[MAX_LIST_ROWS][64];
    int m = 0;

    int n = 0;
    const fault_def_t *list = registry_list(&n);
    for (int i = 0; i < n && m < MAX_LIST_ROWS; i++) {
        rows[m].uid = list[i].uid;
        rows[m].module = list[i].module;
        char buf[64];
        strncpy(buf, list[i].supported_ops, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        snprintf(opsbuf[m], sizeof(opsbuf[m]), "%s", buf);
        rows[m].ops = opsbuf[m];
        rows[m].desc = list[i].desc;
        m++;
    }
    /* 动态插件纳入 list */
    int pc = 0;
    const dcat_plugin_t *const *plugs = plugin_list(&pc);
    for (int i = 0; i < pc && m < MAX_LIST_ROWS; i++) {
        if (plugs[i]->name && strcmp(plugs[i]->name, "sample") == 0) continue;
        rows[m].uid = plugs[i]->uid;
        rows[m].module = plugs[i]->name ? plugs[i]->name : "";
        char pbuf[64];
        strncpy(pbuf, plugs[i]->supported_ops, sizeof(pbuf) - 1);
        pbuf[sizeof(pbuf) - 1] = '\0';
        snprintf(opsbuf[m], sizeof(opsbuf[m]), "%s", pbuf);
        rows[m].ops = opsbuf[m];
        rows[m].desc = (plugs[i]->description && plugs[i]->description[0]) ? plugs[i]->description : "";
        m++;
    }

    /* 计算各列最大宽度(含表头) */
    int w_uid = 4, w_mod = 6, w_ops = 3, w_desc = 4;
    for (int i = 0; i < m; i++) {
        int lu = (int)strlen(rows[i].uid), lm = (int)strlen(rows[i].module),
            lo = (int)strlen(rows[i].ops), ld = (int)strlen(rows[i].desc);
        if (lu > w_uid) w_uid = lu;
        if (lm > w_mod) w_mod = lm;
        if (lo > w_ops) w_ops = lo;
        if (ld > w_desc) w_desc = ld;
    }
    if (w_desc > 60) w_desc = 60; /* 描述列过长则截断，保持可读 */

    size_t cap = (size_t)(m + 2) * (size_t)(w_uid + w_mod + w_ops + w_desc + 4) + 64;
    char *out = malloc(cap);
    if (!out) return result_err("list", NULL, 1, "out of memory");
    size_t off = 0;
#define LF_APPEND(...)                                        \
    do {                                                      \
        int lf = snprintf(out + off, cap - off, __VA_ARGS__); \
        if (lf < 0) {                                         \
            off = 0;                                          \
            break;                                            \
        }                                                     \
        off += (size_t)lf;                                    \
        if (off >= cap) {                                     \
            off = cap - 2;                                    \
            break;                                            \
        }                                                     \
    } while (0)
    const char *dash = "----------------------------------------------------------------------------------------------------";
    LF_APPEND("%-*s  %-*s  %-*s  %s\n", w_uid, "uid", w_mod, "module", w_ops, "ops", "desc");
    LF_APPEND("%.*s  %.*s  %.*s  %.*s\n", w_uid, dash, w_mod, dash, w_ops, dash, w_desc, dash);
    for (int i = 0; i < m; i++) {
        char d[61];
        strncpy(d, rows[i].desc, sizeof(d) - 1);
        d[sizeof(d) - 1] = '\0';
        LF_APPEND("%-*s  %-*s  %-*s  %s\n", w_uid, rows[i].uid, w_mod, rows[i].module, w_ops, rows[i].ops, d);
    }
#undef LF_APPEND
    out[off] = '\0';
    result_t *r = malloc(sizeof(result_t));
    r->code = 0;
    r->json = out;
    r->raw = 1;
    return r;
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
        if ((size_t)w >= cap - len) {
            out[cap - 1] = '\0';
            break;
        }
        len += (size_t)w;
    }
}

/* stateless clean（无参 / clean --all）成功后 reconcile state：把该 uid 全部活跃记录
 * 标 inactive，使 query 与系统一致（不残留幽灵记录）。内存空（state 丢失/无记录）时
 * state_find_by_params 返回 0，自然 no-op。 */
static void reconcile_uid_state(const char *uid) {
    params_t empty;
    params_init(&empty);
    long long ids[DCAT_MAX_RECORDS];
    int n = state_find_by_params(uid, &empty, ids, DCAT_MAX_RECORDS);
    for (int i = 0; i < n; i++) state_mark_inactive(ids[i]);
}

static result_t *cnf_clean(const fault_def_t *f, const params_t *user_params) {
    /* clean <uid> 无参 = clean-all-for-uid：脚本自行 glob /tmp 工件，绕过 state 查找；
     * 脚本成功后 reconcile：把该 uid 全部活跃记录标 inactive，避免 query 残留幽灵。 */
    if (user_params->count == 0) {
        result_t *r = executor_run_fault(f, "clean", user_params, 0);
        if (r && r->code == 0) reconcile_uid_state(f->uid);
        return r;
    }
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

/* clean --all：遍历全部注册故障(cnf + 动态插件)，对支持 clean 的逐个执行无参 clean；
 * stateless，不依赖 state.json（脚本自行 glob /tmp 工件，插件自行幂等清理）。聚合每 uid 结果。
 * 输出：全成功 → 一行摘要；有失败 → 表格 + 汇总（人类可读）。 */
result_t *dispatch_clean_all(void) {
    params_t empty;
    params_init(&empty);
    int n = 0;
    const fault_def_t *list = registry_list(&n);
    int ok = 0, err = 0;
    struct {
        char uid[64];
        int failed;
    } rows[128];
    int rc = 0;

    for (int i = 0; i < n; i++) {
        if (!op_in_supported(list[i].supported_ops, "clean")) continue;
        result_t *r = executor_run_fault(&list[i], "clean", &empty, 0);
        int failed = !(r && r->code == 0);
        if (!failed) {
            reconcile_uid_state(list[i].uid);
            ok++;
        } else {
            err++;
        }
        if (rc < 128) {
            strncpy(rows[rc].uid, list[i].uid, 63);
            rows[rc].uid[63] = '\0';
            rows[rc].failed = failed;
            rc++;
        }
        if (r) result_free(r);
    }

    /* 动态插件同样纳入 clean --all（注入器不在此列，无注册表迭代接口） */
    int pn = 0;
    const dcat_plugin_t *const *plist = plugin_list(&pn);
    for (int i = 0; i < pn; i++) {
        const dcat_plugin_t *p = plist[i];
        if (!op_in_supported(p->supported_ops, "clean") || !p->clean) continue;
        result_t *r = p->clean(&empty);
        int failed = !(r && r->code == 0);
        if (!failed) {
            reconcile_uid_state(p->uid);
            ok++;
        } else {
            err++;
        }
        if (rc < 128) {
            strncpy(rows[rc].uid, p->uid, 63);
            rows[rc].uid[63] = '\0';
            rows[rc].failed = failed;
            rc++;
        }
        if (r) result_free(r);
    }

    result_t *res = malloc(sizeof(result_t));
    res->code = (err > 0) ? 1 : 0;
    if (err == 0) {
        char *buf = malloc(128);
        snprintf(buf, 128, "cleaned %d faults (all ok)\n", ok);
        res->json = buf;
        res->raw = 1;
        return res;
    }
    /* 有失败：表格 + 汇总 */
    size_t cap = 256 + (size_t)rc * 80;
    char *buf = malloc(cap);
    size_t off = 0;
    off += snprintf(buf + off, cap - off, "%-24s  %s\n", "UID", "RESULT");
    off += snprintf(buf + off, cap - off, "%-24s  %s\n", "------------------------", "------");
    for (int i = 0; i < rc; i++) {
        if (off >= cap - 80) {
            off += snprintf(buf + off, cap - off, "... (truncated)\n");
            break;
        }
        off += snprintf(buf + off, cap - off, "%-24s  %s\n", rows[i].uid, rows[i].failed ? "error" : "cleaned");
    }
    off += snprintf(buf + off, cap - off, "\n%d cleaned, %d error\n", ok, err);
    res->json = buf;
    res->raw = 1;
    return res;
}

/* 重注入冲突错误（退出码 5）：列出重叠记录 id 与参数，提示 --force。 */
static result_t *reinject_conflict_err(const char *uid, const long long *ids, int on) {
    char msg[512];
    int off = 0;
    int w = snprintf(msg, sizeof msg, "resource already injected");
    if (w < 0)
        off = (int)sizeof msg;
    else
        off = w;
    int shown = on < 3 ? on : 3;
    for (int k = 0; k < shown && off < (int)sizeof msg; k++) {
        const injection_record_t *rec = state_find_by_id(ids[k]);
        char pstr[256];
        pstr[0] = '\0';
        if (rec) fmt_record_params(rec, pstr, sizeof pstr);
        const char *sep = (k == 0) ? " (" : "; ";
        w = snprintf(msg + off, sizeof msg - (size_t)off,
                     "%srecord id %lld: %s", sep, ids[k], pstr);
        if (w < 0) {
            off = (int)sizeof msg;
            break;
        }
        off += w;
    }
    if (on > 3 && off < (int)sizeof msg) {
        w = snprintf(msg + off, sizeof msg - (size_t)off, "; +%d more", on - 3);
        if (w < 0)
            off = (int)sizeof msg;
        else
            off += w;
    }
    if (off < (int)sizeof msg)
        snprintf(msg + off, sizeof msg - (size_t)off, "); use --force to replace");
    return result_err("inject", uid, DCAT_REINJECT_CONFLICT, msg);
}

/* --force：先清理重叠记录再注入。clean 由调用方提供（cnf=脚本）。
 * 注：cnf 的 clean_one_record 成功返回 NULL（内部已 mark inactive）。 */
typedef result_t *(*force_clean_fn)(void *ctx, const injection_record_t *rec);
static result_t *force_clean_overlap(const char *uid, void *ctx, const long long *ids, int on,
                                     force_clean_fn clean_one) {
    for (int k = 0; k < on; k++) {
        const injection_record_t *rec = state_find_by_id(ids[k]);
        if (!rec) continue;
        result_t *rc = clean_one(ctx, rec);
        if (rc && rc->code != 0) {
            char m[320];
            const char *detail = "";
            cJSON *root = cJSON_Parse(rc->json);
            if (root) {
                cJSON *e = cJSON_GetObjectItem(root, "error");
                cJSON *mj = e ? cJSON_GetObjectItem(e, "message") : NULL;
                if (mj && cJSON_IsString(mj)) detail = mj->valuestring;
            }
            snprintf(m, sizeof m, "--force: clean of record %lld failed: %s", ids[k], detail);
            cJSON_Delete(root);
            result_free(rc);
            return result_err("inject", uid, 1, m);
        }
        result_free(rc);
        state_mark_inactive(ids[k]);
    }
    return NULL;
}

static result_t *cnf_clean_ctx(void *ctx, const injection_record_t *rec) {
    return clean_one_record((const fault_def_t *)ctx, rec->record_id);
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
    if (strcmp(op, "inject") == 0)
        op_req = p->inject_required;
    else if (strcmp(op, "clean") == 0)
        op_req = p->clean_required;
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
    int n = cJSON_GetArraySize(arr);
    if (n > 1) {
        /* 多条记录：表格输出（人类可读） */
        size_t cap = 256 + (size_t)n * 256;
        char *out = malloc(cap);
        if (!out) {
            cJSON_Delete(arr);
            return result_err("query", NULL, 1, "out of memory");
        }
        size_t off = 0;
        off += snprintf(out + off, cap - off, "%-6s  %-24s  %-6s  %-20s  %s\n",
                        "ID", "UID", "ACT", "STARTED", "PARAMS");
        off += snprintf(out + off, cap - off, "%-6s  %-24s  %-6s  %-20s  %s\n",
                        "------", "------------------------", "------", "--------------------", "------");
        cJSON *o;
        cJSON_ArrayForEach(o, arr) {
            cJSON *jid = cJSON_GetObjectItem(o, "record_id");
            cJSON *juid = cJSON_GetObjectItem(o, "uid");
            cJSON *jactive = cJSON_GetObjectItem(o, "active");
            cJSON *jstarted = cJSON_GetObjectItem(o, "started_at");
            cJSON *jparams = cJSON_GetObjectItem(o, "params");
            char pstr[256] = "";
            int plen = 0;
            if (jparams) {
                cJSON *jp;
                cJSON_ArrayForEach(jp, jparams) {
                    if (jp->string && jp->valuestring)
                        plen += snprintf(pstr + plen, sizeof(pstr) - plen, "%s=%s ", jp->string, jp->valuestring);
                }
            }
            if (off >= cap - 256) {
                off += snprintf(out + off, cap - off, "... (truncated)\n");
                break;
            }
            off += snprintf(out + off, cap - off, "%-6d  %-24s  %-6s  %-20s  %s\n",
                            jid ? (int)jid->valuedouble : 0,
                            juid ? juid->valuestring : "",
                            (jactive && jactive->type == cJSON_True) ? "yes" : "no",
                            jstarted ? jstarted->valuestring : "",
                            pstr);
        }
        cJSON_Delete(arr);
        out[off] = '\0';
        result_t *r = malloc(sizeof(result_t));
        r->code = 0;
        r->json = out;
        r->raw = 1;
        return r;
    }
    /* 0-1 条记录：保持 JSON */
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddStringToObject(root, "op", "query");
    cJSON_AddItemToObject(root, "data", arr);
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    result_t *r = malloc(sizeof(result_t));
    r->code = 0;
    r->json = s;
    r->raw = 0;
    return r;
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
            if (on > 0 && !force) return reinject_conflict_err(f->uid, ids, on);
            if (on > 0 && force) {
                result_t *fc = force_clean_overlap(f->uid, (void *)f, ids, on, cnf_clean_ctx);
                if (fc) return fc;
            }
            return cnf_inject(f, params);
        }
        if (strcmp(op, "clean") == 0) return cnf_clean(f, params);
        if (strcmp(op, "query") == 0) {
            int rc = executor_run_raw_fault(f, "query", params);
            printf("---\n");
            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "status", "ok");
            cJSON_AddStringToObject(root, "op", "query");
            cJSON_AddStringToObject(root, "uid", uid);
            cJSON *data = cJSON_AddObjectToObject(root, "data");
            cJSON_AddBoolToObject(data, "confirmed", rc == 0);
            char *s = cJSON_PrintUnformatted(root);
            cJSON_Delete(root);
            result_t *r = malloc(sizeof(result_t));
            r->code = 0;
            r->json = s;
            r->raw = 0;
            return r;
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
                state_mark_inactive(ids[i]);
                result_free(r);
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
