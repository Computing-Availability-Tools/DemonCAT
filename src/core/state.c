/* src/core/state.c */
#include "state.h"
#include "cJSON.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static injection_record_t g_records[DCAT_MAX_RECORDS];
static int g_next_id = 1;
static char g_path[256];
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static void copystr(char *dst, size_t cap, const char *src) {
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

/* Check if user params match a record's stored params.
 * Empty user params (count==0) matches all. */
static int params_match(const params_t *user, const params_t *stored) {
    if (!user || user->count == 0) return 1;
    for (int i = 0; i < user->count; i++) {
        int found = 0;
        for (int j = 0; j < stored->count; j++) {
            if (!strcmp(user->items[i].key, stored->items[j].key) &&
                !strcmp(user->items[i].value, stored->items[j].value)) {
                found = 1;
                break;
            }
        }
        if (!found) return 0;
    }
    return 1;
}

int state_init(const char *persist_path) {
    pthread_mutex_lock(&g_lock);
    memset(g_records, 0, sizeof g_records);
    g_next_id = 1;
    if (persist_path) {
        copystr(g_path, sizeof g_path, persist_path);
    } else {
        g_path[0] = '\0';
    }
    pthread_mutex_unlock(&g_lock);
    return 0;
}

int state_add(const char *uid, const params_t *params) {
    pthread_mutex_lock(&g_lock);
    int slot = -1;
    for (int i = 0; i < DCAT_MAX_RECORDS; i++) {
        if (!g_records[i].active) { slot = i; break; }
    }
    int id = 0;
    if (slot >= 0) {
        injection_record_t *r = &g_records[slot];
        memset(r, 0, sizeof *r);
        r->record_id = g_next_id++;
        copystr(r->uid, sizeof r->uid, uid ? uid : "");
        if (params) r->params = *params;
        r->started_at = time(NULL);
        r->active = 1;
        id = r->record_id;
    }
    pthread_mutex_unlock(&g_lock);
    if (id) state_save();
    return id;
}

injection_record_t state_find(const char *uid) {
    injection_record_t res;
    memset(&res, 0, sizeof res);
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < DCAT_MAX_RECORDS; i++) {
        if (g_records[i].active && !strcmp(g_records[i].uid, uid ? uid : "")) {
            res = g_records[i];
            break;
        }
    }
    pthread_mutex_unlock(&g_lock);
    return res;
}

int state_find_by_params(const char *uid, const params_t *params,
                         injection_record_t out[], int max_out) {
    int n = 0;
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < DCAT_MAX_RECORDS && n < max_out; i++) {
        if (g_records[i].active && !strcmp(g_records[i].uid, uid ? uid : "")) {
            if (params_match(params, &g_records[i].params)) {
                out[n] = g_records[i];
                n++;
            }
        }
    }
    pthread_mutex_unlock(&g_lock);
    return n;
}

int state_record(int idx, injection_record_t *out) {
    if (idx < 0 || idx >= DCAT_MAX_RECORDS || !out) return 0;
    pthread_mutex_lock(&g_lock);
    int active = g_records[idx].active;
    if (active) *out = g_records[idx];
    pthread_mutex_unlock(&g_lock);
    return active;
}

int state_count_active(void) {
    int n = 0;
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < DCAT_MAX_RECORDS; i++) if (g_records[i].active) n++;
    pthread_mutex_unlock(&g_lock);
    return n;
}

void state_mark_inactive(int record_id) {
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < DCAT_MAX_RECORDS; i++) {
        if (g_records[i].record_id == record_id && g_records[i].active) {
            g_records[i].active = 0;
            break;
        }
    }
    pthread_mutex_unlock(&g_lock);
    state_save();
}

int state_save(void) {
    if (!g_path[0]) return 0;

    char dir[256];
    copystr(dir, sizeof dir, g_path);
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        if (dir[0]) {
            for (char *p = dir + 1; *p; p++) {
                if (*p == '/') { *p = '\0'; mkdir(dir, 0755); *p = '/'; }
            }
            mkdir(dir, 0755);
        }
    }

    pthread_mutex_lock(&g_lock);
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < DCAT_MAX_RECORDS; i++) {
        if (!g_records[i].record_id) continue;
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "record_id", g_records[i].record_id);
        cJSON_AddStringToObject(o, "uid", g_records[i].uid);
        cJSON_AddNumberToObject(o, "started_at", (double)g_records[i].started_at);
        cJSON_AddBoolToObject(o, "active", g_records[i].active);
        cJSON *parr = cJSON_CreateArray();
        for (int j = 0; j < g_records[i].params.count; j++) {
            cJSON *pkv = cJSON_CreateObject();
            cJSON_AddStringToObject(pkv, "key", g_records[i].params.items[j].key);
            cJSON_AddStringToObject(pkv, "value", g_records[i].params.items[j].value);
            cJSON_AddItemToArray(parr, pkv);
        }
        cJSON_AddItemToObject(o, "params", parr);
        cJSON_AddItemToArray(arr, o);
    }
    char *s = cJSON_PrintUnformatted(arr);
    char tmp[288];
    snprintf(tmp, sizeof tmp, "%s.tmp", g_path);
    FILE *f = fopen(tmp, "w");
    int ok = 0;
    if (f) {
        fputs(s, f);
        fclose(f);
        ok = (rename(tmp, g_path) == 0);
    }
    free(s);
    cJSON_Delete(arr);
    pthread_mutex_unlock(&g_lock);
    return ok ? 0 : -1;
}

int state_load(void) {
    if (!g_path[0]) return 0;
    FILE *f = fopen(g_path, "r");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return 0; }
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return -1; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';
    cJSON *arr = cJSON_Parse(buf);
    free(buf);
    if (!arr) return -1;

    pthread_mutex_lock(&g_lock);
    memset(g_records, 0, sizeof g_records);
    int maxid = 0;
    int i = 0;
    cJSON *o;
    cJSON_ArrayForEach(o, arr) {
        if (i >= DCAT_MAX_RECORDS) break;
        injection_record_t *r = &g_records[i++];
        cJSON *rid = cJSON_GetObjectItem(o, "record_id");
        cJSON *u = cJSON_GetObjectItem(o, "uid");
        cJSON *sa = cJSON_GetObjectItem(o, "started_at");
        cJSON *ac = cJSON_GetObjectItem(o, "active");
        cJSON *pa = cJSON_GetObjectItem(o, "params");
        if (rid) r->record_id = (int)rid->valuedouble;
        if (u && cJSON_IsString(u)) copystr(r->uid, sizeof r->uid, u->valuestring);
        if (sa) r->started_at = (time_t)sa->valuedouble;
        if (ac) r->active = cJSON_IsTrue(ac) ? 1 : 0;
        if (pa && cJSON_IsArray(pa)) {
            cJSON *pkv;
            cJSON_ArrayForEach(pkv, pa) {
                if (r->params.count >= DCAT_MAX_PARAMS) break;
                cJSON *k = cJSON_GetObjectItem(pkv, "key");
                cJSON *v = cJSON_GetObjectItem(pkv, "value");
                if (k && v && cJSON_IsString(k) && cJSON_IsString(v)) {
                    copystr(r->params.items[r->params.count].key,
                            sizeof r->params.items[r->params.count].key, k->valuestring);
                    copystr(r->params.items[r->params.count].value,
                            sizeof r->params.items[r->params.count].value, v->valuestring);
                    r->params.count++;
                }
            }
        }
        if (r->record_id > maxid) maxid = r->record_id;
    }
    g_next_id = maxid + 1;
    pthread_mutex_unlock(&g_lock);
    cJSON_Delete(arr);
    return 0;
}
