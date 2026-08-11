#include "types.h"
#include <string.h>
#include <ctype.h>

void params_init(params_t *p) { p->count = 0; }

int params_set(params_t *p, const char *key, const char *val) {
    for (int i = 0; i < p->count; i++) {
        if (strcmp(p->items[i].key, key) == 0) {
            strncpy(p->items[i].value, val, DCAT_VAL_LEN - 1);
            p->items[i].value[DCAT_VAL_LEN - 1] = '\0';
            return 0;
        }
    }
    if (p->count >= DCAT_MAX_PARAMS) return -1;
    strncpy(p->items[p->count].key, key, DCAT_KEY_LEN - 1);
    p->items[p->count].key[DCAT_KEY_LEN - 1] = '\0';
    strncpy(p->items[p->count].value, val, DCAT_VAL_LEN - 1);
    p->items[p->count].value[DCAT_VAL_LEN - 1] = '\0';
    p->count++;
    return 0;
}

const char *params_find(const params_t *p, const char *key) {
    for (int i = 0; i < p->count; i++)
        if (strcmp(p->items[i].key, key) == 0) return p->items[i].value;
    return NULL;
}

const char *dcat_key_to_env(const char *key) {
    static char buf[64];
    int n = 0;
    strcpy(buf, "DCAT_PARAM_");
    n = (int)strlen(buf);
    for (const char *c = key; *c && n < (int)sizeof(buf) - 1; c++) {
        buf[n++] = isalnum((unsigned char)*c) ? (char)toupper((unsigned char)*c) : '_';
    }
    buf[n] = '\0';
    return buf;
}

int params_match_subset(const params_t *query, const params_t *record) {
    /* query 每个 key �?record 中存在且值一致则匹配；空 query 匹配所�?*/
    for (int i = 0; i < query->count; i++) {
        const char *v = params_find(record, query->items[i].key);
        if (!v || strcmp(v, query->items[i].value) != 0) return 0;
    }
    return 1;
}
