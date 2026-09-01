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
#include <sys/file.h>

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

/* 跨进程文件锁：state.json 的写入必须持锁做 read-modify-write，否则并发 CLI/web
 * 进程各自 load→改→save，后写者覆盖先写者 → 注入记录丢失（C1）。改值型读回校验、
 * clean 按 state 找齐注入都会失真。锁文件与数据文件分离（rename 会更换 inode，
 * 直接 flock 数据文件会失效）。读(load)走 rename 原子替换，无需锁。 */
static int state_lock(const char *path) {
    char lpath[300];
    snprintf(lpath, sizeof lpath, "%s.lck", path);
    int fd = open(lpath, O_CREAT | O_RDWR, 0600);
    if (fd < 0) return -1;
    if (flock(fd, LOCK_EX) != 0) { /* 排他锁，序列化读改写 */
        close(fd);
        return -1;
    }
    return fd;
}

/* 从文件解析全部记录到 out（独立于全局 g_records，供 save 合并）。返回记录数；
 * 文件缺失/损坏返回 -1。next_id_out 收到文件里保存的 next_id。 */
static int parse_records_file(const char *path, injection_record_t out[DCAT_MAX_RECORDS],
                              long long *next_id_out) {
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz < 0) {
        fclose(fp);
        return -1;
    }
    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(fp);
        return -1;
    }
    size_t rd = fread(buf, 1, (size_t)sz, fp);
    buf[rd] = '\0';
    fclose(fp);
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) return -1;
    long long nid = 1;
    cJSON *nidj = cJSON_GetObjectItem(root, "next_id");
    if (nidj) nid = (long long)nidj->valuedouble;
    if (next_id_out) *next_id_out = nid;
    int i = 0;
    cJSON *arr = cJSON_GetObjectItem(root, "records");
    cJSON *o;
    cJSON_ArrayForEach(o, arr) {
        if (i >= DCAT_MAX_RECORDS) break;
        memset(&out[i], 0, sizeof(out[i]));
        cJSON *rid = cJSON_GetObjectItem(o, "record_id");
        cJSON *uid = cJSON_GetObjectItem(o, "uid");
        cJSON *prms = cJSON_GetObjectItem(o, "params");
        cJSON *sa = cJSON_GetObjectItem(o, "started_at");
        cJSON *ac = cJSON_GetObjectItem(o, "active");
        if (rid) out[i].record_id = (long long)rid->valuedouble;
        if (uid) {
            strncpy(out[i].uid, uid->valuestring, sizeof(out[i].uid) - 1);
            out[i].uid[sizeof(out[i].uid) - 1] = '\0';
        }
        if (prms) json_to_params(prms, &out[i].params);
        if (sa && cJSON_IsString(sa)) {
            strncpy(out[i].started_at, sa->valuestring, sizeof(out[i].started_at) - 1);
            out[i].started_at[sizeof(out[i].started_at) - 1] = '\0';
        }
        if (ac) out[i].active = cJSON_IsTrue(ac);
        i++;
    }
    cJSON_Delete(root);
    return i;
}

static int record_identical(const injection_record_t *a, const injection_record_t *b) {
    if (a->record_id != b->record_id) return 0;
    if (strcmp(a->uid, b->uid) != 0) return 0;
    if (a->params.count != b->params.count) return 0;
    for (int i = 0; i < a->params.count; i++) {
        const char *v = params_find(&b->params, a->params.items[i].key);
        if (!v || strcmp(v, a->params.items[i].value) != 0) return 0;
    }
    return 1;
}

/* 同逻辑记录（忽略 record_id）：uid+params 全等即视为同一条注入。
 * 并发注入重编号后内存 record_id 与磁盘不一致（W1），id 只是序号，不能作匹配键。 */
static int record_same_params(const injection_record_t *a, const injection_record_t *b) {
    if (strcmp(a->uid, b->uid) != 0) return 0;
    if (a->params.count != b->params.count) return 0;
    for (int i = 0; i < a->params.count; i++) {
        const char *v = params_find(&b->params, a->params.items[i].key);
        if (!v || strcmp(v, a->params.items[i].value) != 0) return 0;
    }
    return 1;
}

/* 把 mem 记录并入 disk 记录：保留磁盘上他人新增的记录，id 撞车时给本地新记录
 * 分配新 id（两进程各自从 next_id=1 起步时都会拿到 record_id=1）。
 * 匹配键：先按 record_id+uid+params（重复 save / clean 回写精确命中），再按
 * uid+params 忽略 id 匹配磁盘上同逻辑记录（并发重编号后内存 id 已失真，找磁盘
 * active 的目标记录，只覆盖 active/started_at，避免产生 ghost 且 clean 失效）。 */
static int merge_records(const injection_record_t *mem, int mem_n,
                         const injection_record_t *disk, int disk_n,
                         injection_record_t out[DCAT_MAX_RECORDS], long long *next_id_out) {
    int n = 0;
    long long maxid = *next_id_out > 1 ? *next_id_out - 1 : 0;
    for (int i = 0; i < disk_n && n < DCAT_MAX_RECORDS; i++) {
        out[n++] = disk[i];
        if (disk[i].record_id > maxid) maxid = disk[i].record_id;
    }
    for (int i = 0; i < mem_n && n < DCAT_MAX_RECORDS; i++) {
        const injection_record_t *m = &mem[i];
        int matched = 0;
        int j;
        /* 1) 精确匹配：record_id+uid+params 全等（本进程重复 save / clean 后回写） */
        for (j = 0; j < n; j++) {
            if (record_identical(m, &out[j])) {
                out[j].active = m->active;
                strncpy(out[j].started_at, m->started_at, sizeof(out[j].started_at) - 1);
                out[j].started_at[sizeof(out[j].started_at) - 1] = '\0';
                matched = 1;
                break;
            }
        }
        /* 2) 逻辑匹配：忽略 id，按 uid+params 找磁盘上同逻辑记录（并发重编号后
         *    内存持旧 id，record_id 已对不上）。clean 回写 inactive 须命中磁盘那条
         *    active 目标；重复 save 命中磁盘同逻辑记录同步 started_at。 */
        if (!matched) {
            for (j = 0; j < n; j++) {
                if (record_same_params(m, &out[j])) {
                    out[j].active = m->active;
                    strncpy(out[j].started_at, m->started_at, sizeof(out[j].started_at) - 1);
                    out[j].started_at[sizeof(out[j].started_at) - 1] = '\0';
                    matched = 1;
                    break;
                }
            }
        }
        if (matched) continue;
        int id_taken = 0;
        for (j = 0; j < n; j++) {
            if (out[j].record_id == m->record_id) {
                id_taken = 1;
                break;
            }
        }
        if (id_taken) { /* 与其他进程注入撞 id → 分配全新 id，两条都保留 */
            injection_record_t copy = *m;
            copy.record_id = ++maxid;
            out[n++] = copy;
        } else {
            if (m->record_id > maxid) maxid = m->record_id;
            out[n++] = *m;
        }
    }
    *next_id_out = maxid + 1;
    return n;
}

void state_save(void) {
    pthread_mutex_lock(&g_lock);
    if (!g_dirty) { /* 只读操作(查询/列表)不重写 state 文件 */
        pthread_mutex_unlock(&g_lock);
        return;
    }
    injection_record_t mem[DCAT_MAX_RECORDS];
    int mem_n = 0;
    long long next_seed = g_next_id;
    for (int i = 0; i < DCAT_MAX_RECORDS; i++) {
        if (g_records[i].record_id == 0) continue;
        mem[mem_n++] = g_records[i];
    }
    g_dirty = 0; /* 清脏标记在锁内：避免并发 state_add 的 dirty=1 被覆盖 */
    pthread_mutex_unlock(&g_lock);

    char path[256];
    expand_home_path(g_file, path, sizeof(path));
    ensure_parent_dir(path);
    int lfd = state_lock(path);
    if (lfd < 0) { /* 锁失败仍尽力写（单进程场景不退化），不静默丢数据 */
        pthread_mutex_lock(&g_lock);
        g_dirty = 1;
        pthread_mutex_unlock(&g_lock);
        return;
    }

    /* 持锁重读磁盘 → 与内存记录合并：保留并发进程刚写入的记录 */
    injection_record_t disk[DCAT_MAX_RECORDS];
    long long disk_next = 1;
    int disk_n = parse_records_file(path, disk, &disk_next);
    if (disk_n < 0) {
        disk_n = 0;
        disk_next = 1;
    }
    if (disk_n > DCAT_MAX_RECORDS) disk_n = DCAT_MAX_RECORDS;

    injection_record_t merged[DCAT_MAX_RECORDS];
    long long merged_next = disk_next > 1 ? disk_next : 1;
    if (next_seed > merged_next) merged_next = next_seed;
    int mn = merge_records(mem, mem_n, disk, disk_n, merged, &merged_next);

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < mn; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "record_id", (double)merged[i].record_id);
        cJSON_AddStringToObject(o, "uid", merged[i].uid);
        cJSON_AddItemToObject(o, "params", params_to_json(&merged[i].params));
        cJSON_AddStringToObject(o, "started_at", merged[i].started_at);
        cJSON_AddBoolToObject(o, "active", merged[i].active);
        cJSON_AddItemToArray(arr, o);
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "next_id", (double)merged_next);
    cJSON_AddItemToObject(root, "records", arr);
    char *s = cJSON_Print(root);
    cJSON_Delete(root);

    int saved = 0;
    if (s) {
        /* Atomic write: temp file + rename to prevent truncated JSON on crash */
        char tmp[300];
        snprintf(tmp, sizeof(tmp), "%s.tmp", path);
        FILE *fp = fopen(tmp, "w");
        if (fp) {
            int rc = fputs(s, fp);
            if (fclose(fp) == 0 && rc != EOF && rename(tmp, path) == 0)
                saved = 1;
            else
                unlink(tmp);
        }
        free(s);
    }
    close(lfd);
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
    memset(g_records, 0, sizeof(g_records)); /* clear stale records before loading */
    g_next_id = 1;                           /* 0 is the empty-slot sentinel; start at 1 */
    char path[256];
    expand_home_path(g_file, path, sizeof(path));
    FILE *fp = fopen(path, "r");
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
