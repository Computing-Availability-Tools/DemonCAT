#include "core/config.h"
#include "core/registry.h"
#include "core/state.h"
#include "core/executor.h"
#include "core/output.h"
#include "core/dispatch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char last_cmd[256];
static char last_op[16];
static int calls;

static result_t *mock(const char *cmd) {
    strncpy(last_cmd, cmd, sizeof last_cmd - 1);
    last_cmd[sizeof last_cmd - 1] = '\0';
    const char *op = getenv("DCAT_OP");
    strncpy(last_op, op ? op : "", sizeof last_op - 1);
    last_op[sizeof last_op - 1] = '\0';
    calls++;
    return result_ok("inject", "x", NULL);
}

#define CK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", #cond); return 1; } } while (0)

int main(void) {
    config_t cfg;
    CK(config_load("config/demoncat.conf", &cfg) == 0);
    CK(registry_init(&cfg) == 0);
    state_init("/tmp/dcat_cs.json");
    state_set_clean_cb(dispatch_clean_record);
    executor_set_mock(mock);

    /* rCPU_core_offline (sync) */
    params_t pc; memset(&pc, 0, sizeof pc);
    pc.count = 1;
    strcpy(pc.items[0].key, "cores"); strcpy(pc.items[0].value, "0,2,4");
    calls = 0;
    result_t *r = dispatch_inject(registry_find("rCPU_core_offline"), &pc);
    CK(r->code == 0); CK(strstr(last_cmd, "cpu_core_offline.sh") != NULL);
    CK(getenv("DCAT_PARAM_CORES") && !strcmp(getenv("DCAT_PARAM_CORES"), "0,2,4"));
    result_free(r);
    calls = 0;
    r = dispatch_clean(registry_find("rCPU_core_offline"), &pc);
    CK(r->code == 0); CK(strcmp(last_op, "clean") == 0);
    result_free(r);

    /* rDISK_write_overload (background) */
    params_t pd; memset(&pd, 0, sizeof pd);
    pd.count = 2;
    strcpy(pd.items[0].key, "device"); strcpy(pd.items[0].value, "/data");
    strcpy(pd.items[1].key, "workers"); strcpy(pd.items[1].value, "4");
    calls = 0;
    r = dispatch_inject(registry_find("rDISK_write_overload"), &pd);
    CK(r->code == 0); CK(strstr(last_cmd, "disk_write_overload.sh") != NULL);
    CK(getenv("DCAT_PARAM_DEVICE") && !strcmp(getenv("DCAT_PARAM_DEVICE"), "/data"));
    CK(getenv("DCAT_PARAM_WORKERS") && !strcmp(getenv("DCAT_PARAM_WORKERS"), "4"));
    result_free(r);
    calls = 0;
    r = dispatch_clean(registry_find("rDISK_write_overload"), &pd);
    CK(r->code == 0); CK(calls == 0);  /* background: kill */
    result_free(r);

    unlink("/tmp/dcat_cs.json");
    return 0;
}
