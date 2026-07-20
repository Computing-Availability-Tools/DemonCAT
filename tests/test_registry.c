#include "core/config.h"
#include "core/registry.h"
#include <string.h>

int main(void) {
    config_t cfg;
    if (config_load("config/demoncat.conf", &cfg)) return 1;
    if (cfg.fault_count != 18) return 1;

    if (registry_init(&cfg)) return 1;

    const fault_def_t *f = registry_find("rCPU_overload");
    if (!f || strcmp(f->module, "cpu")) return 1;
    if (f->safety != SAFETY_WARNING) return 1;
    if (f->exec_mode != EXEC_BACKGROUND) return 1;
    if (strcmp(f->script, "config/scripts/cpu/cpu_overload.sh")) return 1;
    /* v0.2: duration migrated required -> optional */
    if (strstr(f->required_params, "duration") != NULL) return 1;
    if (strcmp(f->optional_params, "duration")) return 1;

    const fault_def_t *n = registry_find("rNET_delay");
    if (!n || strcmp(n->module, "network")) return 1;
    if (n->exec_mode != EXEC_SYNC) return 1;
    if (n->safety != SAFETY_NORMAL) return 1;
    if (strcmp(n->script, "config/scripts/network/net_delay.sh")) return 1;
    if (strstr(n->required_params, "duration") != NULL) return 1;
    if (strcmp(n->optional_params, "duration")) return 1;

    if (registry_find("nope") != NULL) return 1;

    int cnt;
    (void)registry_list(&cnt);
    if (cnt != 18) return 1;

    return 0;
}
