#include "state.h"
#include "cJSON.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static injection_record_t g_records[DCAT_MAX_RECORDS];
static int g_next_id = 1;
static char g_path[256];
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static state_clean_cb g_clean_cb = NULL;
static volatile int g_clean_run = 0;
static pthread_t g_clean_thread;

int state_init(const char *persist_path) {
    memset(g_records, 0, sizeof g_records);
    g_next_id = 1;
    if (persist_path) {
        strncpy(g_path, persist_path, sizeof g_path - 1);
        g_path[sizeof g_path - 1] = '\0';
    } else {
        g_path[0] = '\0';
    }
    return 0;
}

int state_add(const char *uid, pid_t pid, int timeout_s) {
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
        strncpy(r->uid, uid ? uid : "", sizeof r->uid - 1);
        r->bg_pid = pid;
        r->started_at = time(NULL);
        r->expires_at = timeout_s > 0 ? r->started_at + timeout_s : 0;
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
            res = g_records[i];   /* snapshot under lock */
            break;
        }
    }
    pthread_mutex_unlock(&g_lock);
    return res;   /* returned by value — caller gets a safe copy */
}

int state_record(int idx, injection_record_t *out) {
    if (idx < 0 || idx >= DCAT_MAX_RECORDS || !out) return 0;
    pthread_mutex_lock(&g_lock);
    int active = g_records[idx].active;
    if (active) *out = g_records[idx];   /* snapshot under lock */
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

    /* ensure parent directory exists (mkdir -p the dirname) */
    char dir[256];
    strncpy(dir, g_path, sizeof dir - 1);
    dir[sizeof dir - 1] = '\0';
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

    /* hold lock for the entire serialize+write so concurrent state_save
     * (auto-clean thread + main thread) can't interleave file writes.
     * write to <path>.tmp then rename(2) for atomicity — prevents a
     * crash mid-write from leaving a truncated/corrupt state file. */
    pthread_mutex_lock(&g_lock);
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < DCAT_MAX_RECORDS; i++) {
        if (!g_records[i].record_id) continue;
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "record_id", g_records[i].record_id);
        cJSON_AddStringToObject(o, "uid", g_records[i].uid);
        cJSON_AddNumberToObject(o, "bg_pid", (double)g_records[i].bg_pid);
        cJSON_AddNumberToObject(o, "started_at", (double)g_records[i].started_at);
        cJSON_AddNumberToObject(o, "expires_at", (double)g_records[i].expires_at);
        cJSON_AddBoolToObject(o, "active", g_records[i].active);
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
        cJSON *p = cJSON_GetObjectItem(o, "bg_pid");
        cJSON *sa = cJSON_GetObjectItem(o, "started_at");
        cJSON *ea = cJSON_GetObjectItem(o, "expires_at");
        cJSON *ac = cJSON_GetObjectItem(o, "active");
        if (rid) r->record_id = (int)rid->valuedouble;
        if (u && cJSON_IsString(u)) strncpy(r->uid, u->valuestring, sizeof r->uid - 1);
        if (p) r->bg_pid = (pid_t)p->valuedouble;
        if (sa) r->started_at = (time_t)sa->valuedouble;
        if (ea) r->expires_at = (time_t)ea->valuedouble;
        if (ac) r->active = cJSON_IsTrue(ac) ? 1 : 0;
        if (r->record_id > maxid) maxid = r->record_id;
    }
    g_next_id = maxid + 1;
    pthread_mutex_unlock(&g_lock);
    cJSON_Delete(arr);
    return 0;
}

void state_set_clean_cb(state_clean_cb cb) { g_clean_cb = cb; }

static void *auto_clean_loop(void *arg) {
    (void)arg;
    while (g_clean_run) {
        injection_record_t copies[DCAT_MAX_RECORDS];
        int n = 0;
        pthread_mutex_lock(&g_lock);
        time_t now = time(NULL);
        for (int i = 0; i < DCAT_MAX_RECORDS; i++) {
            if (g_records[i].active && g_records[i].expires_at > 0 && g_records[i].expires_at <= now) {
                copies[n] = g_records[i];
                g_records[i].active = 0;   /* claim immediately to avoid re-clean */
                n++;
            }
        }
        pthread_mutex_unlock(&g_lock);
        for (int i = 0; i < n; i++) {
            if (g_clean_cb) g_clean_cb(&copies[i]);
        }
        if (n > 0) state_save();
        sleep(1);
    }
    return NULL;
}

int state_auto_clean_start(void) {
    g_clean_run = 1;
    if (pthread_create(&g_clean_thread, NULL, auto_clean_loop, NULL) != 0) {
        g_clean_run = 0;
        return -1;
    }
    return 0;
}

void state_lazy_clean(void) {
    injection_record_t copies[DCAT_MAX_RECORDS];
    int n = 0;
    pthread_mutex_lock(&g_lock);
    time_t now = time(NULL);
    for (int i = 0; i < DCAT_MAX_RECORDS; i++) {
        if (g_records[i].active && g_records[i].expires_at > 0 && g_records[i].expires_at <= now) {
            copies[n] = g_records[i];
            g_records[i].active = 0;
            n++;
        }
    }
    pthread_mutex_unlock(&g_lock);
    for (int i = 0; i < n; i++) {
        if (g_clean_cb) g_clean_cb(&copies[i]);
    }
    if (n > 0) state_save();
}
