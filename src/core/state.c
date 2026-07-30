#include "state.h"
#include <cJSON.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

static injection_record_t g_records[DCAT_MAX_RECORDS];
static int g_next_id = 1;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static char g_file[256] = "~/.demoncat/state.json";

void state_reset(void) {
    pthread_mutex_lock(&g_lock);
    memset(g_records, 0, sizeof(g_records));
    g_next_id = 1;
    pthread_mutex_unlock(&g_lock);
}
void state_set_file(const char *path) {
    pthread_mutex_lock(&g_lock);
    strncpy(g_file, path, sizeof(g_file)-1); g_file[sizeof(g_file)-1]='\0';
    pthread_mutex_unlock(&g_lock);
}

int state_add(const char *uid, const params_t *params) {
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < DCAT_MAX_RECORDS; i++) {
        if (!g_records[i].active) {
            g_records[i].record_id = g_next_id++;
            strncpy(g_records[i].uid, uid, sizeof(g_records[i].uid)-1);
            g_records[i].uid[sizeof(g_records[i].uid)-1]='\0';
            g_records[i].params = *params;
            g_records[i].started_at = (long)time(NULL);
            g_records[i].active = 1;
            int id = g_records[i].record_id;
            pthread_mutex_unlock(&g_lock);
            return id;
        }
    }
    pthread_mutex_unlock(&g_lock);
    return -1;
}

int state_find_by_params(const char *uid, const params_t *query, int *ids, int max_ids) {
    pthread_mutex_lock(&g_lock);
    int n = 0;
    for (int i = 0; i < DCAT_MAX_RECORDS; i++) {
        if (g_records[i].active && strcmp(g_records[i].uid, uid) == 0 &&
            params_match_subset(query, &g_records[i].params)) {
            if (n < max_ids) ids[n] = g_records[i].record_id;
            n++;
        }
    }
    pthread_mutex_unlock(&g_lock);
    return n;
}

const injection_record_t *state_find_by_id(int id) {
    pthread_mutex_lock(&g_lock);
    const injection_record_t *r = NULL;
    for (int i = 0; i < DCAT_MAX_RECORDS; i++)
        if (g_records[i].record_id == id && g_records[i].active) { r = &g_records[i]; break; }
    pthread_mutex_unlock(&g_lock);
    return r;
}

int state_snapshot_by_uid(const char *uid, injection_record_t *out, int max) {
    pthread_mutex_lock(&g_lock);
    int n = 0;
    for (int i = 0; i < DCAT_MAX_RECORDS && n < max; i++) {
        if (g_records[i].active && strcmp(g_records[i].uid, uid) == 0) {
            out[n++] = g_records[i];
        }
    }
    pthread_mutex_unlock(&g_lock);
    return n;
}

int state_list_active(void) {
    pthread_mutex_lock(&g_lock);
    int n = 0;
    for (int i = 0; i < DCAT_MAX_RECORDS; i++) if (g_records[i].active) n++;
    pthread_mutex_unlock(&g_lock);
    return n;
}

void state_mark_inactive(int id) {
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < DCAT_MAX_RECORDS; i++)
        if (g_records[i].record_id == id) { g_records[i].active = 0; break; }
    pthread_mutex_unlock(&g_lock);
}

void state_for_each_active(state_visit_fn fn, void *ctx) {
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < DCAT_MAX_RECORDS; i++)
        if (g_records[i].active) fn(&g_records[i], ctx);
    pthread_mutex_unlock(&g_lock);
}

static cJSON *params_to_json(const params_t *p) {
    cJSON *o = cJSON_CreateObject();
    for (int i = 0; i < p->count; i++)
        cJSON_AddStringToObject(o, p->items[i].key, p->items[i].value);
    return o;
}
static void json_to_params(const cJSON *o, params_t *p) {
    params_init(p);
    if (!o) return;
    cJSON *k;
    cJSON_ArrayForEach(k, o) {
        if (cJSON_IsString(k)) params_set(p, k->string, k->valuestring);
    }
}

/* Expand a leading "~/" (or "~") to $HOME. Falls back to the raw path. */
static void expand_home_path(const char *in, char *out, size_t cap) {
    if (in && in[0] == '~' && (in[1] == '/' || in[1] == '\0')) {
        const char *home = getenv("HOME");
        if (home && home[0]) { snprintf(out, cap, "%s%s", home, in + 1); return; }
    }
    snprintf(out, cap, "%s", in ? in : "");
}

/* mkdir -p for the parent directory of `path`. Best-effort, ignores EEXIST. */
static void ensure_parent_dir(const char *path) {
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", path);
    char *slash = strrchr(buf, '/');
    if (!slash || slash == buf) return;     /* no parent or root */
    *slash = '\0';
    for (char *p = buf + 1; *p; p++) {
        if (*p == '/') { *p = '\0'; mkdir(buf, 0755); *p = '/'; }
    }
    mkdir(buf, 0755);
}

void state_save(void) {
    pthread_mutex_lock(&g_lock);
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < DCAT_MAX_RECORDS; i++) {
        if (!g_records[i].active) continue;
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "record_id", g_records[i].record_id);
        cJSON_AddStringToObject(o, "uid", g_records[i].uid);
        cJSON_AddItemToObject(o, "params", params_to_json(&g_records[i].params));
        cJSON_AddNumberToObject(o, "started_at", g_records[i].started_at);
        cJSON_AddBoolToObject(o, "active", g_records[i].active);
        cJSON_AddItemToArray(arr, o);
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "next_id", g_next_id);
    cJSON_AddItemToObject(root, "records", arr);
    char *s = cJSON_Print(root); cJSON_Delete(root);
    pthread_mutex_unlock(&g_lock);

    if (!s) return;
    char path[256];
    expand_home_path(g_file, path, sizeof(path));
    ensure_parent_dir(path);
    FILE *fp = fopen(path, "w");
    if (fp) {
        if (fputs(s, fp) == EOF || fclose(fp) != 0) {
            fclose(fp);
        }
    }
    free(s);
}

void state_load(void) {
    pthread_mutex_lock(&g_lock);
    char path[256];
    expand_home_path(g_file, path, sizeof(path));
    FILE *fp = fopen(path, "r");
    if (!fp) { pthread_mutex_unlock(&g_lock); return; }
    fseek(fp, 0, SEEK_END); long sz = ftell(fp); fseek(fp, 0, SEEK_SET);
    if (sz < 0) { fclose(fp); pthread_mutex_unlock(&g_lock); return; }
    char *buf = malloc((size_t)sz + 1);
    size_t rd = fread(buf, 1, (size_t)sz, fp); buf[rd] = '\0'; fclose(fp);
    cJSON *root = cJSON_Parse(buf); free(buf);
    if (root) {
        cJSON *nid = cJSON_GetObjectItem(root, "next_id");
        if (nid) g_next_id = nid->valueint;
        cJSON *arr = cJSON_GetObjectItem(root, "records");
        cJSON *o; int i = 0;
        cJSON_ArrayForEach(o, arr) {
            if (i >= DCAT_MAX_RECORDS) break;
            cJSON *rid = cJSON_GetObjectItem(o, "record_id");
            cJSON *uid = cJSON_GetObjectItem(o, "uid");
            cJSON *prms = cJSON_GetObjectItem(o, "params");
            cJSON *sa = cJSON_GetObjectItem(o, "started_at");
            cJSON *ac = cJSON_GetObjectItem(o, "active");
            if (rid) g_records[i].record_id = rid->valueint;
            if (uid) { strncpy(g_records[i].uid, uid->valuestring, sizeof(g_records[i].uid)-1); g_records[i].uid[sizeof(g_records[i].uid)-1]='\0'; }
            if (prms) json_to_params(prms, &g_records[i].params);
            if (sa) g_records[i].started_at = (long)sa->valuedouble;
            if (ac) g_records[i].active = cJSON_IsTrue(ac);
            i++;
        }
        cJSON_Delete(root);
    }
    pthread_mutex_unlock(&g_lock);
}
