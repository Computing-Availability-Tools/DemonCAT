#include "registry.h"
#include <string.h>

static fault_def_t g_faults[DCAT_MAX_FAULTS];
static int g_count = 0;

void registry_init(const config_t *cfg) {
    g_count = cfg->fault_count < DCAT_MAX_FAULTS ? cfg->fault_count : DCAT_MAX_FAULTS;
    for (int i = 0; i < g_count; i++) g_faults[i] = cfg->faults[i];
}

const fault_def_t *registry_find(const char *uid) {
    for (int i = 0; i < g_count; i++)
        if (strcmp(g_faults[i].uid, uid) == 0) return &g_faults[i];
    return NULL; /* 未命中：dispatch 回退 injector_find（DESIGN §7.4�?*/
}

const fault_def_t *registry_list(int *count) { if (count) *count = g_count; return g_faults; }
int registry_count(void) { return g_count; }
