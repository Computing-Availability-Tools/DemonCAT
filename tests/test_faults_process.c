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
    state_init("/tmp/dcat_proc.json");
    state_set_clean_cb(dispatch_clean_record);
    executor_set_mock(mock);

    /* rPROC_exit — inject-only: no state, no record_id, clean/query rejected */
    params_t pe; memset(&pe, 0, sizeof pe);
    pe.count = 1;
    strcpy(pe.items[0].key, "pid"); strcpy(pe.items[0].value, "12345");
    calls = 0;
    result_t *r = dispatch_inject(registry_find("rPROC_exit"), &pe);
    CK(r->code == 0); CK(calls == 1);
    CK(strstr(last_cmd, "proc_exit.sh") != NULL);
    CK(strcmp(last_op, "inject") == 0);
    CK(getenv("DCAT_PARAM_PID") && !strcmp(getenv("DCAT_PARAM_PID"), "12345"));
    CK(!state_find("rPROC_exit").record_id);  /* inject-only: no state record */
    result_free(r);
    calls = 0;
    r = dispatch_clean(registry_find("rPROC_exit"), &pe);
    CK(r->code == DCAT_E_SAFETY);  /* op not in supported_ops=inject */
    CK(calls == 0);
    result_free(r);

    /* rPROC_dstate (background) */
    params_t pd; memset(&pd, 0, sizeof pd);
    pd.count = 1;
    strcpy(pd.items[0].key, "count"); strcpy(pd.items[0].value, "2");
    calls = 0;
    r = dispatch_inject(registry_find("rPROC_dstate"), &pd);
    CK(r->code == 0); CK(strstr(last_cmd, "proc_dstate.sh") != NULL);
    CK(getenv("DCAT_PARAM_COUNT") && !strcmp(getenv("DCAT_PARAM_COUNT"), "2"));
    result_free(r);
    calls = 0;
    r = dispatch_clean(registry_find("rPROC_dstate"), &pd);
    CK(r->code == 0); CK(calls == 0);  /* background: kill */
    result_free(r);

    /* rPROC_hang (sync) */
    params_t ph; memset(&ph, 0, sizeof ph);
    ph.count = 1;
    strcpy(ph.items[0].key, "pid"); strcpy(ph.items[0].value, "9999");
    calls = 0;
    r = dispatch_inject(registry_find("rPROC_hang"), &ph);
    CK(r->code == 0); CK(strstr(last_cmd, "proc_hang.sh") != NULL);
    CK(getenv("DCAT_PARAM_PID") && !strcmp(getenv("DCAT_PARAM_PID"), "9999"));
    result_free(r);
    calls = 0;
    r = dispatch_clean(registry_find("rPROC_hang"), &ph);
    CK(r->code == 0); CK(strcmp(last_op, "clean") == 0);
    result_free(r);

    /* rPROC_zstate (background) */
    params_t pz; memset(&pz, 0, sizeof pz);
    pz.count = 1;
    strcpy(pz.items[0].key, "count"); strcpy(pz.items[0].value, "3");
    calls = 0;
    r = dispatch_inject(registry_find("rPROC_zstate"), &pz);
    CK(r->code == 0); CK(strstr(last_cmd, "proc_zstate.sh") != NULL);
    result_free(r);
    calls = 0;
    r = dispatch_clean(registry_find("rPROC_zstate"), &pz);
    CK(r->code == 0); CK(calls == 0);  /* background: kill */
    result_free(r);

    unlink("/tmp/dcat_proc.json");
    return 0;
}
