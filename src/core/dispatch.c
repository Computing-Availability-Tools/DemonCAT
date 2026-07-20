#include "dispatch.h"
#include "registry.h"
#include "state.h"
#include "executor.h"
#include "safety.h"
#include "output.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *safety_str(safety_level_t s) {
    if (s == SAFETY_WARNING) return "warning";
    if (s == SAFETY_DANGEROUS) return "dangerous";
    return "normal";
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

result_t *dispatch_list(void) {
    int n;
    const fault_def_t *t = registry_list(&n);
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < n; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "uid", t[i].uid);
        cJSON_AddStringToObject(o, "module", t[i].module);
        cJSON_AddStringToObject(o, "safety", safety_str(t[i].safety));
        cJSON_AddItemToObject(o, "supported_ops", ops_array(t[i].supported_ops));
        cJSON_AddStringToObject(o, "desc", t[i].desc);
        cJSON_AddItemToArray(arr, o);
    }
    return result_ok("list", "", arr);
}

result_t *dispatch_query(const char *uid, const params_t *p) {
    /* no uid: return all active state records (state-only query) */
    if (!uid || !uid[0]) {
        cJSON *arr = cJSON_CreateArray();
        for (int i = 0; i < DCAT_MAX_RECORDS; i++) {
            injection_record_t r;
            if (!state_record(i, &r)) continue;
            cJSON *o = cJSON_CreateObject();
            cJSON_AddNumberToObject(o, "record_id", r.record_id);
            cJSON_AddStringToObject(o, "uid", r.uid);
            cJSON_AddNumberToObject(o, "started_at", (double)r.started_at);
            cJSON_AddNumberToObject(o, "expires_at", (double)r.expires_at);
            cJSON_AddBoolToObject(o, "active", r.active);
            cJSON_AddItemToArray(arr, o);
        }
        return result_ok("query", "", arr);
    }

    /* uid provided: run the script's query branch to verify fault on system.
     * Script stdout/stderr flow directly to the user's terminal (tables,
     * multi-line text), then dcat prints "---" separator + JSON result. */
    const fault_def_t *f = registry_find(uid);
    if (!f) return result_err("query", uid, DCAT_E_NOTFOUND, "uid not found");

    /* precheck: query must be in supported_ops + script executable */
    result_t *pc = safety_precheck(f, "query", p);
    if (!pc) return result_err("query", uid, DCAT_E_RUN, "internal error");
    if (pc->code != 0) return pc;
    result_free(pc);

    char cmd[256];
    executor_build_cmd(f, "query", p, cmd, sizeof cmd);
    executor_set_env("query", uid, p);

    /* run script directly — raw output goes to terminal, dcat only gets exit code */
    int rc = executor_run_raw(cmd);

    /* print separator between raw output and JSON */
    printf("---\n");
    fflush(stdout);

    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "confirmed", rc == 0);
    return result_ok("query", uid, data);
}

static int param_int(const params_t *p, const char *key) {
    for (int i = 0; i < p->count; i++)
        if (!strcmp(p->items[i].key, key)) return atoi(p->items[i].value);
    return -1;
}

void dispatch_clean_record(const injection_record_t *rec) {
    if (!rec) return;
    const fault_def_t *f = registry_find(rec->uid);
    if (!f) { state_mark_inactive(rec->record_id); return; }
    if (rec->bg_pid > 0) {
        executor_kill(rec->bg_pid);
    } else {
        char cmd[256];
        executor_build_cmd(f, "clean", NULL, cmd, sizeof cmd);
        executor_set_env("clean", f->uid, NULL);
        result_t *r = executor_run(cmd, 30000);
        if (r && r->code != 0)
            fprintf(stderr, "[dcat] WARN: auto-clean script for %s exited %d — fault may persist\n",
                    rec->uid, r->code);
        result_free(r);
    }
    state_mark_inactive(rec->record_id);
}

static int is_inject_only(const fault_def_t *f) {
    /* supported_ops contains no "clean" -> inject-only (one-shot, no state) */
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

result_t *dispatch_inject(const fault_def_t *f, const params_t *p) {
    result_t *pc = safety_precheck(f, "inject", p);
    if (!pc) return result_err("inject", f->uid, DCAT_E_RUN, "internal error");
    if (pc->code != 0) return pc;
    result_free(pc);

    char cmd[256];
    executor_build_cmd(f, "inject", p, cmd, sizeof cmd);
    executor_set_env("inject", f->uid, p);

    cJSON *data = cJSON_CreateObject();

    if (is_inject_only(f)) {
        result_t *rr = executor_run(cmd, 30000);
        if (rr->code != 0) {
            cJSON_Delete(data);
            result_free(rr);
            return result_err("inject", f->uid, DCAT_E_RUN, "script failed");
        }
        /* wire data.message from script stdout (SPEC §6) */
        cJSON *rr_root = cJSON_Parse(rr->json);
        if (rr_root) {
            cJSON *rr_data = cJSON_GetObjectItem(rr_root, "data");
            cJSON *msg = rr_data ? cJSON_GetObjectItem(rr_data, "message") : NULL;
            if (msg && msg->valuestring && msg->valuestring[0])
                cJSON_AddStringToObject(data, "message", msg->valuestring);
            cJSON_Delete(rr_root);
        }
        result_free(rr);
        return result_ok("inject", f->uid, data);
    }

    if (state_find(f->uid).record_id) {
        cJSON_Delete(data);
        return result_err("inject", f->uid, DCAT_E_SAFETY, "already active");
    }

    int dur = param_int(p, "duration");
    if (dur < 0) dur = f->timeout;

    pid_t bgpid = 0;
    if (f->exec_mode == EXEC_BACKGROUND) {
        pid_t pid = executor_spawn(cmd);
        if (pid < 0) { cJSON_Delete(data); return result_err("inject", f->uid, DCAT_E_RUN, "spawn failed"); }
        bgpid = pid;
        cJSON_AddNumberToObject(data, "pid", (double)pid);
    } else {
        /* C8: use a fixed 30s script-execution timeout — `duration` is how long
         * the fault PERSISTS (governed by state + reaper), NOT how long the
         * script takes to apply. Conflating them kills slow scripts mid-apply. */
        result_t *rr = executor_run(cmd, 30000);
        if (!rr || rr->code != 0) {
            cJSON_Delete(data);
            result_free(rr);
            return result_err("inject", f->uid, DCAT_E_RUN, "script failed");
        }
        /* C7: wire data.message from script stdout — same as inject-only branch */
        cJSON *rr_root = cJSON_Parse(rr->json);
        if (rr_root) {
            cJSON *rr_data = cJSON_GetObjectItem(rr_root, "data");
            cJSON *msg = rr_data ? cJSON_GetObjectItem(rr_data, "message") : NULL;
            if (msg && msg->valuestring && msg->valuestring[0])
                cJSON_AddStringToObject(data, "message", msg->valuestring);
            cJSON_Delete(rr_root);
        }
        result_free(rr);
    }
    int id = state_add(f->uid, bgpid, dur);
    cJSON_AddNumberToObject(data, "record_id", id);
    return result_ok("inject", f->uid, data);
}

result_t *dispatch_clean(const fault_def_t *f, const params_t *p) {
    result_t *pc = safety_precheck(f, "clean", p);
    if (!pc) return result_err("clean", f->uid, DCAT_E_RUN, "internal error");
    if (pc->code != 0) return pc;
    result_free(pc);

    injection_record_t rec = state_find(f->uid);
    if (!rec.record_id) return result_err("clean", f->uid, DCAT_E_RUN, "no active injection");
    if (rec.bg_pid > 0) {
        executor_kill(rec.bg_pid);
    } else {
        char cmd[256];
        executor_build_cmd(f, "clean", p, cmd, sizeof cmd);
        executor_set_env("clean", f->uid, p);
        result_t *r = executor_run(cmd, 30000);
        if (!r || r->code != 0) {
            /* C2: clean script failed — fault may still be active, DON'T mark inactive */
            result_free(r);
            return result_err("clean", f->uid, DCAT_E_RUN, "clean script failed");
        }
        result_free(r);
    }
    int rid = rec.record_id;
    state_mark_inactive(rid);
    cJSON *data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "record_id", rid);
    return result_ok("clean", f->uid, data);
}

void dispatch_build_reaper(const char *exe, const char *uid, const char *cfgpath,
                           int dur, char *buf, size_t len) {
    if (!buf || len == 0) return;
    buf[0] = '\0';
    if (!exe || !uid || dur <= 0) return;
    /* `clean <uid>` is quoted so the shell hands it to dcat as ONE argv element;
     * main.c's argv loop overwrites cmdarg on each non-flag arg, so without
     * quotes `clean` and `<uid>` would be separate args and cmdarg would end up
     * as just <uid>, failing cli_parse with "parse error".
     * exe and cfgpath are single-quoted to handle paths with spaces and prevent
     * shell metacharacter injection. When cfgpath is NULL/empty, omit --config
     * entirely so the reaper's dcat finds its own default config (C5+C6 fix). */
    if (!cfgpath || !cfgpath[0]) {
        snprintf(buf, len, "sleep %d; '%s' \"clean %s\" --yes",
                 dur, exe, uid);
    } else {
        snprintf(buf, len, "sleep %d; '%s' \"clean %s\" --config '%s' --yes",
                 dur, exe, uid, cfgpath);
    }
}
