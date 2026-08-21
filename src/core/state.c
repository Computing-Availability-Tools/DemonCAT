#include "state.h"
#include <cJSON.h>
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

static injection_record_t g_records[DCAT_MAX_RECORDS];
static long long g_next_id = 1;
#define DCAT_MAX_ID ((long long)9000000000000000000LL)
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static char g_file[256] = "~/.demoncat/state.json";
static int g_state_lost = 0; /* 1=state 文件缺失或 JSON 解析失败(损坏/截断) */
static int g_dirty = 0;      /* 1=有记录变更(注入/清理),state_save 才落盘 */

static void fmt_started_at(time_t t, char *buf, size_t n) {
    struct tm tmv;
    if (localtime_r(&t, &tmv) && strftime(buf, n, "%Y-%m-%d %H:%M:%S", &tmv) > 0) return;
    buf[0] = '\0';
}

void state_reset(void) {
    pthread_mutex_lock(&g_lock);
    memset(g_records, 0, sizeof(g_records));
    g_next_id = 1;
    g_state_lost = 0;
    g_dirty = 1; /* 内存已清空，可能与磁盘不一致 → 下次 state_save 须落盘 */
    pthread_mutex_unlock(&g_lock);
}
void state_set_file(const char *path) {
    pthread_mutex_lock(&g_lock);
    strncpy(g_file, path, sizeof(g_file) - 1);
    g_file[sizeof(g_file) - 1] = '\0';
    pthread_mutex_unlock(&g_lock);
}

long long state_add(const char *uid, const params_t *params) {
    pthread_mutex_lock(&g_lock);
    if (g_next_id > DCAT_MAX_ID) {
        pthread_mutex_unlock(&g_lock);
        return -1;
    }
    int slot = -1;
    /* 优先取从未用过的空槽(record_id==0),保留已清理记录的历史; */
    for (int i = 0; i < DCAT_MAX_RECORDS; i++) {
        if (g_records[i].record_id == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        /* 空槽耗尽才回退到最旧的 inactive(已清理)槽 — 历史会被覆盖 */
        for (int i = 0; i < DCAT_MAX_RECORDS; i++) {
            if (!g_records[i].active) {
                slot = i;
                break;
            }
        }
    }
    if (slot < 0) {
        pthread_mutex_unlock(&g_lock);
        return -1;
    } /* 表满(全活跃) */
    g_records[slot].record_id = g_next_id++;
    strncpy(g_records[slot].uid, uid, sizeof(g_records[slot].uid) - 1);
    g_records[slot].uid[sizeof(g_records[slot].uid) - 1] = '\0';
    g_records[slot].params = *params;
    fmt_started_at(time(NULL), g_records[slot].started_at, sizeof(g_records[slot].started_at));
    g_records[slot].active = 1;
    long long id = g_records[slot].record_id;
    g_dirty = 1;
    pthread_mutex_unlock(&g_lock);
    return id;
}

int state_find_by_params(const char *uid, const params_t *query, long long *ids, int max_ids) {
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

const injection_record_t *state_find_by_id(long long id) {
    pthread_mutex_lock(&g_lock);
    const injection_record_t *r = NULL;
    for (int i = 0; i < DCAT_MAX_RECORDS; i++)
        if (g_records[i].record_id == id && g_records[i].active) {
            r = &g_records[i];
            break;
        }
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
    for (int i = 0; i < DCAT_MAX_RECORDS; i++)
        if (g_records[i].active) n++;
    pthread_mutex_unlock(&g_lock);
    return n;
}

void state_mark_inactive(long long id) {
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < DCAT_MAX_RECORDS; i++)
        if (g_records[i].record_id == id) {
            g_records[i].active = 0;
            g_dirty = 1;
            break;
        }
    pthread_mutex_unlock(&g_lock);
}

void state_for_each_active(state_visit_fn fn, void *ctx) {
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < DCAT_MAX_RECORDS; i++)
        if (g_records[i].active) fn(&g_records[i], ctx);
    pthread_mutex_unlock(&g_lock);
}

void state_for_each_all(state_visit_fn fn, void *ctx) {
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < DCAT_MAX_RECORDS; i++)
        if (g_records[i].record_id != 0) fn(&g_records[i], ctx);
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
        if (home && home[0]) {
            snprintf(out, cap, "%s%s", home, in + 1);
            return;
        }
    }
    snprintf(out, cap, "%s", in ? in : "");
}

/* mkdir -p for the parent directory of `path`. Best-effort, ignores EEXIST. */
static void ensure_parent_dir(const char *path) {
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", path);
    char *slash = strrchr(buf, '/');
    if (!slash || slash == buf) return; /* no parent or root */
    *slash = '\0';
    for (char *p = buf + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(buf, 0755);
            *p = '/';
        }
    }
    mkdir(buf, 0755);
}

void state_save(void) {
    pthread_mutex_lock(&g_lock);
    if (!g_dirty) { /* 只读操作(查询/列表)不重写 state 文件 */
        pthread_mutex_unlock(&g_lock);
        return;
    }
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < DCAT_MAX_RECORDS; i++) {
        if (g_records[i].record_id == 0) continue; /* 写全部已用记录(活跃+已清理),保留历史 */
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "record_id", (double)g_records[i].record_id);
        cJSON_AddStringToObject(o, "uid", g_records[i].uid);
        cJSON_AddItemToObject(o, "params", params_to_json(&g_records[i].params));
        cJSON_AddStringToObject(o, "started_at", g_records[i].started_at);
        cJSON_AddBoolToObject(o, "active", g_records[i].active);
        cJSON_AddItemToArray(arr, o);
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "next_id", (double)g_next_id);
    cJSON_AddItemToObject(root, "records", arr);
    char *s = cJSON_Print(root);
    cJSON_Delete(root);
    g_dirty = 0; /* 清脏标记在锁内：避免并发 state_add 的 dirty=1 被覆盖 */
    pthread_mutex_unlock(&g_lock);

    int saved = 0;
    if (s) {
        char path[256];
        expand_home_path(g_file, path, sizeof(path));
        ensure_parent_dir(path);
        /* Atomic write: temp file + rename to prevent truncated JSON on crash */
        char tmp[300];
        snprintf(tmp, sizeof(tmp), "%s.tmp", path);
        FILE *fp = fopen(tmp, "w");
        if (fp) {
            int rc = fputs(s, fp);
            if (fclose(fp) == 0 && rc != EOF && rename(tmp, path) == 0) saved = 1;
            else unlink(tmp);
        }
        free(s);
    }
    if (!saved) {
        /* 写盘失败(cJSON_Print 返回 NULL / fopen 失败 / fputs 失败 / fclose 失败)：
         * 恢复脏标记，下次 state_save 重试，不静默丢失。 */
        pthread_mutex_lock(&g_lock);
        g_dirty = 1;
        pthread_mutex_unlock(&g_lock);
    }
}

void state_load(void) {
    pthread_mutex_lock(&g_lock);
    g_dirty = 0;
    memset(g_records, 0, sizeof(g_records));  /* clear stale records before loading */
    g_next_id = 0;
    FILE *fp = fopen(g_file, "r");
    if (!fp) {
        g_state_lost = 1;
        pthread_mutex_unlock(&g_lock);
        return;
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz < 0) {
        fclose(fp);
        pthread_mutex_unlock(&g_lock);
        return;
    }
    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(fp);
        pthread_mutex_unlock(&g_lock);
        return;
    }
    size_t rd = fread(buf, 1, (size_t)sz, fp);
    buf[rd] = '\0';
    fclose(fp);
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (root) {
        g_state_lost = 0;
        cJSON *nid = cJSON_GetObjectItem(root, "next_id");
        if (nid) g_next_id = (long long)nid->valuedouble;
        cJSON *arr = cJSON_GetObjectItem(root, "records");
        cJSON *o;
        int i = 0;
        cJSON_ArrayForEach(o, arr) {
            if (i >= DCAT_MAX_RECORDS) break;
            cJSON *rid = cJSON_GetObjectItem(o, "record_id");
            cJSON *uid = cJSON_GetObjectItem(o, "uid");
            cJSON *prms = cJSON_GetObjectItem(o, "params");
            cJSON *sa = cJSON_GetObjectItem(o, "started_at");
            cJSON *ac = cJSON_GetObjectItem(o, "active");
            if (rid) g_records[i].record_id = (long long)rid->valuedouble;
            if (uid) {
                strncpy(g_records[i].uid, uid->valuestring, sizeof(g_records[i].uid) - 1);
                g_records[i].uid[sizeof(g_records[i].uid) - 1] = '\0';
            }
            if (prms) json_to_params(prms, &g_records[i].params);
            if (sa) {
                if (cJSON_IsString(sa) && sa->valuestring[0]) {
                    strncpy(g_records[i].started_at, sa->valuestring, sizeof(g_records[i].started_at) - 1);
                    g_records[i].started_at[sizeof(g_records[i].started_at) - 1] = '\0';
                } else if (cJSON_IsNumber(sa)) {
                    fmt_started_at((time_t)sa->valuedouble, g_records[i].started_at, sizeof(g_records[i].started_at));
                } else {
                    g_records[i].started_at[0] = '\0';
                }
            } else {
                g_records[i].started_at[0] = '\0';
            }
            if (ac) g_records[i].active = cJSON_IsTrue(ac);
            i++;
        }
        cJSON_Delete(root);
    } else {
        g_state_lost = 1;
        fprintf(stderr, "dcat: state file corrupt or unreadable, ignoring: %s\n", g_file);
    }
    pthread_mutex_unlock(&g_lock);
}

int state_is_lost(void) {
    pthread_mutex_lock(&g_lock);
    int v = g_state_lost;
    pthread_mutex_unlock(&g_lock);
    return v;
}
