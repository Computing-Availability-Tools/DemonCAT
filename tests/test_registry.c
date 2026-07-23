/* tests/test_registry.c */
#include "core/config.h"
#include "core/registry.h"
#include <string.h>

int main(void) {
    config_t cfg;
    if (config_load("config/demoncat.conf", &cfg)) return 1;
    if (cfg.fault_count != 38) return 1; /* 2 cpu + 1 storage + 11 network + 4 process + 20 npu */

    if (registry_init(&cfg)) return 1;

    /* rCPU_overload */
    const fault_def_t *cpu = registry_find("rCPU_overload");
    if (!cpu || strcmp(cpu->module, "cpu")) return 1;
    if (strcmp(cpu->supported_ops, "inject,clean,query")) return 1;
    if (strcmp(cpu->required_params, "cores")) return 1;
    if (cpu->optional_params[0] != '\0') return 1;

    /* rNET_loss */
    const fault_def_t *net = registry_find("rNET_loss");
    if (!net || strcmp(net->module, "network")) return 1;
    if (strcmp(net->required_params, "iface,loss_pct")) return 1;

    /* rPROC_exit (inject-only) */
    const fault_def_t *proc = registry_find("rPROC_exit");
    if (!proc || strcmp(proc->supported_ops, "inject")) return 1;
    if (strcmp(proc->required_params, "pid")) return 1;

    /* not found */
    if (registry_find("nope") != NULL) return 1;

    /* list count */
    int cnt;
    (void)registry_list(&cnt);
    if (cnt != 38) return 1;

    return 0;
}
