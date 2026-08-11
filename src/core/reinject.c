#include "reinject.h"
#include "state.h"
#include <string.h>
#include <stdlib.h>

static int is_uint(const char *s) {
    if (!s || !s[0]) return 0;
    for (const char *p = s; *p; p++)
        if (*p < '0' || *p > '9') return 0;
    return 1;
}

int cores_parse(const char *spec, unsigned char bits[DCAT_CORES_BYTES]) {
    if (!spec || !spec[0] || !bits) return -1;
    size_t len = strlen(spec);
    if (spec[0] == ',' || spec[len - 1] == ',') return -1;
    if (strstr(spec, ",,")) return -1;
    if (len > 255) return -1;                    /* �?buf 截断; 现实 cores 规格远短于此 */

    memset(bits, 0, DCAT_CORES_BYTES);
    char buf[256];
    strncpy(buf, spec, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *save = NULL;
    char *tok = strtok_r(buf, ",", &save);
    if (!tok) return -1;
    while (tok) {
        char *dash = strchr(tok, '-');
        if (dash) {
            *dash = '\0';
            if (!is_uint(tok) || strlen(tok) > 4 ||
                !is_uint(dash + 1) || strlen(dash + 1) > 4) return -1;  /* �?atoi 溢出 */
            long lo = atol(tok);
            long hi = atol(dash + 1);
            if (lo > hi || hi >= DCAT_MAX_CORES) return -1;
            for (long n = lo; n <= hi; n++) bits[n / 8] |= (unsigned char)(1 << (n % 8));
        } else {
            if (!is_uint(tok) || strlen(tok) > 4) return -1;
            long n = atol(tok);
            if (n >= DCAT_MAX_CORES) return -1;
            bits[n / 8] |= (unsigned char)(1 << (n % 8));
        }
        tok = strtok_r(NULL, ",", &save);
    }
    return 0;
}

int cores_intersect(const unsigned char a[DCAT_CORES_BYTES], const unsigned char b[DCAT_CORES_BYTES]) {
    if (!a || !b) return 0;
    for (int i = 0; i < DCAT_CORES_BYTES; i++)
        if (a[i] & b[i]) return 1;
    return 0;
}

/* 资源键各参数是否重叠：cores→集合交集，其余→精确等；缺参跳�?留给 precheck)�? * new_params 未声明任何资源参�?如无参插�? �?不判冲突(无资源可冲突)�? * clean_required 为空且带参注�?�?保守判重�?任意活动=冲突)�?*/
static int resource_overlaps(const params_t *new_params, const params_t *rec_params,
                             const char *clean_required) {
    if (!new_params || new_params->count == 0) return 0;
    char buf[128];
    strncpy(buf, clean_required ? clean_required : "", sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    if (buf[0] == '\0') return 1;
    char *save = NULL;
    char *tok = strtok_r(buf, ",", &save);
    while (tok) {
        const char *nv = params_find(new_params, tok);
        const char *rv = params_find(rec_params, tok);
        if (!nv || !rv) { tok = strtok_r(NULL, ",", &save); continue; }
        if (strcmp(tok, "cores") == 0) {
            unsigned char nb[DCAT_CORES_BYTES], rb[DCAT_CORES_BYTES];
            if (cores_parse(nv, nb) != 0) return 0;   /* 新参 malformed �?不判 overlap, 留给脚本报错 */
            if (cores_parse(rv, rb) != 0) { tok = strtok_r(NULL, ",", &save); continue; }  /* 记录异常 �?跳过 */
            if (!cores_intersect(nb, rb)) return 0;
        } else {
            if (strcmp(nv, rv) != 0) return 0;
        }
        tok = strtok_r(NULL, ",", &save);
    }
    return 1;
}

int reinject_find_overlap_ops(const char *uid, const char *clean_required,
                              const params_t *new_params, long long *out_ids, int max_ids) {
    if (!uid || !new_params) return 0;
    injection_record_t snap[DCAT_MAX_RECORDS];
    int n = state_snapshot_by_uid(uid, snap, DCAT_MAX_RECORDS);
    int cnt = 0;
    for (int i = 0; i < n; i++) {
        if (snap[i].active &&
            resource_overlaps(new_params, &snap[i].params, clean_required)) {
            if (out_ids && cnt < max_ids) out_ids[cnt] = snap[i].record_id;
            cnt++;
        }
    }
    return cnt;
}

int reinject_find_overlap(const fault_def_t *f, const params_t *new_params,
                          long long *out_ids, int max_ids) {
    if (!f) return 0;
    return reinject_find_overlap_ops(f->uid, f->clean_required, new_params, out_ids, max_ids);
}
