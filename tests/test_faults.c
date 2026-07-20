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
    state_init("/tmp/dcat_faults.json");
    state_set_clean_cb(dispatch_clean_record);
    executor_set_mock(mock);

    /* ---- rCPU_overload inject (background) ---- */
    params_t pcpu;
    memset(&pcpu, 0, sizeof pcpu);
    pcpu.count = 2;
    strcpy(pcpu.items[0].key, "cores");
    strcpy(pcpu.items[0].value, "2");
    strcpy(pcpu.items[1].key, "duration");
    strcpy(pcpu.items[1].value, "5");
    calls = 0;
    result_t *r = dispatch_inject(registry_find("rCPU_overload"), &pcpu);
    CK(r->code == 0);
    CK(calls == 1);                          /* background spawn calls mock once */
    CK(strstr(last_cmd, "cpu_overload.sh") != NULL);
    CK(strcmp(last_op, "inject") == 0);
    CK(getenv("DCAT_PARAM_CORES") && !strcmp(getenv("DCAT_PARAM_CORES"), "2"));
    CK(getenv("DCAT_PARAM_DURATION") && !strcmp(getenv("DCAT_PARAM_DURATION"), "5"));
    result_free(r);

    /* ---- rCPU_overload clean (background -> executor_kill mock no-op, no run) ---- */
    calls = 0;
    r = dispatch_clean(registry_find("rCPU_overload"), &pcpu);
    CK(r->code == 0);
    CK(calls == 0);
    result_free(r);

    /* ---- rNET_delay inject (sync) ---- */
    params_t pnet;
    memset(&pnet, 0, sizeof pnet);
    pnet.count = 2;
    strcpy(pnet.items[0].key, "iface");
    strcpy(pnet.items[0].value, "eth0");
    strcpy(pnet.items[1].key, "delay_ms");
    strcpy(pnet.items[1].value, "100");
    calls = 0;
    r = dispatch_inject(registry_find("rNET_delay"), &pnet);
    CK(r->code == 0);
    CK(calls == 1);                          /* sync run calls mock once */
    CK(strstr(last_cmd, "net_delay.sh") != NULL);
    CK(strcmp(last_op, "inject") == 0);
    CK(getenv("DCAT_PARAM_IFACE") && !strcmp(getenv("DCAT_PARAM_IFACE"), "eth0"));
    result_free(r);

    /* ---- rNET_delay clean (sync -> executor_run with DCAT_OP=clean) ---- */
    calls = 0;
    r = dispatch_clean(registry_find("rNET_delay"), &pnet);
    CK(r->code == 0);
    CK(calls == 1);
    CK(strcmp(last_op, "clean") == 0);
    result_free(r);

    /* ---- query reflects (now empty) active set ---- */
    r = dispatch_query("", NULL);
    CK(r->code == 0);
    result_free(r);

    /* ---- inject-only fault (supported_ops=inject): no state, no record_id ---- */
    {
        fault_def_t f;
        memset(&f, 0, sizeof f);
        strcpy(f.uid, "rTEST_oneoff");
        strcpy(f.module, "process");
        strcpy(f.script, "/bin/true");
        strcpy(f.supported_ops, "inject");
        strcpy(f.required_params, "pid");
        f.safety = SAFETY_DANGEROUS;
        f.exec_mode = EXEC_SYNC;

        params_t po;
        memset(&po, 0, sizeof po);
        po.count = 1;
        strcpy(po.items[0].key, "pid");
        strcpy(po.items[0].value, "999");

        calls = 0;
        r = dispatch_inject(&f, &po);
        CK(r->code == 0);
        CK(calls == 1);                          /* executor_run called once */
        CK(strcmp(last_op, "inject") == 0);
        CK(getenv("DCAT_PARAM_PID") && !strcmp(getenv("DCAT_PARAM_PID"), "999"));
        /* inject-only: no record_id in output, no state record */
        CK(!state_find("rTEST_oneoff").record_id);
        CK(strstr(r->json, "record_id") == NULL);  /* inject-only: no record_id in output */
        result_free(r);

        /* clean on inject-only must be rejected at precheck (code 3) */
        calls = 0;
        r = dispatch_clean(&f, &po);
        CK(r->code == DCAT_E_SAFETY);
        CK(calls == 0);  /* executor never called: precheck rejected before run */
        result_free(r);
    }

    /* ---- duration-expiry auto-recovery: sync fault with duration, wait past
     * expiry, state_lazy_clean runs dispatch_clean_record -> executor_set_env(
     * "clean", uid, NULL). Without the NULL guard this SIGSEGVs (Fix #1). ---- */
    {
        params_t ploss;
        memset(&ploss, 0, sizeof ploss);
        ploss.count = 3;
        strcpy(ploss.items[0].key, "iface");
        strcpy(ploss.items[0].value, "eth0");
        strcpy(ploss.items[1].key, "loss_pct");
        strcpy(ploss.items[1].value, "5");
        strcpy(ploss.items[2].key, "duration");
        strcpy(ploss.items[2].value, "1");

        r = dispatch_inject(registry_find("rNET_loss"), &ploss);
        CK(r->code == 0);
        result_free(r);

        injection_record_t rec = state_find("rNET_loss");
        CK(rec.record_id != 0);             /* active record created */
        CK(rec.expires_at > 0);          /* duration applied -> has expiry */

        sleep(2);                         /* record now expired */

        calls = 0;
        last_op[0] = '\0';
        state_lazy_clean();              /* startup auto-clean path (Fix #1 path) */

        CK(calls >= 1);                   /* mock ran the clean script */
        CK(strcmp(last_op, "clean") == 0);
        CK(!state_find("rNET_loss").record_id);   /* cleaned -> inactive */
    }

    unlink("/tmp/dcat_faults.json");
    return 0;
}
