#include "registry.h"
#include <string.h>

static fault_def_t g_table[DCAT_MAX_FAULTS];
static int g_count;

int registry_init(const config_t *cfg) {
    if (!cfg) return -1;
    g_count = cfg->fault_count > DCAT_MAX_FAULTS ? DCAT_MAX_FAULTS : cfg->fault_count;
    for (int i = 0; i < g_count; i++) g_table[i] = cfg->faults[i];
    return 0;
}

const fault_def_t *registry_find(const char *uid) {
    if (!uid) return NULL;
    for (int i = 0; i < g_count; i++)
        if (!strcmp(g_table[i].uid, uid)) return &g_table[i];
    return NULL;
}

const fault_def_t *registry_list(int *count) {
    if (count) *count = g_count;
    return g_table;
}
