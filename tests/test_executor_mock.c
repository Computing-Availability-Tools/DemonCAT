/* tests/test_executor_mock.c */
#include "core/executor.h"
#include "core/output.h"
#include <stdlib.h>
#include <string.h>

static char last_cmd[256];
static int mock_called;

static result_t *mock(const char *cmd) {
    strncpy(last_cmd, cmd, sizeof last_cmd - 1);
    last_cmd[sizeof last_cmd - 1] = '\0';
    mock_called = 1;
    return result_ok("inject", "x", NULL);
}

int main(void) {
    fault_def_t f;
    memset(&f, 0, sizeof f);
    strcpy(f.uid, "rCPU_overload");
    strcpy(f.script, "config/scripts/cpu/cpu_overload.sh");

    params_t p;
    memset(&p, 0, sizeof p);
    p.count = 2;
    strcpy(p.items[0].key, "cores");
    strcpy(p.items[0].value, "4");
    strcpy(p.items[1].key, "loss_pct");
    strcpy(p.items[1].value, "5");

    /* build_cmd: just copies script path */
    char buf[256];
    executor_build_cmd(&f, "inject", &p, buf, sizeof buf);
    if (strcmp(buf, "config/scripts/cpu/cpu_overload.sh")) return 1;

    /* set_env: sets DCAT_OP, DCAT_UID, DCAT_PARAM_CORES, DCAT_PARAM_LOSS_PCT */
    executor_set_env("inject", "rCPU_overload", &p);
    if (!getenv("DCAT_OP") || strcmp(getenv("DCAT_OP"), "inject")) return 1;
    if (!getenv("DCAT_UID") || strcmp(getenv("DCAT_UID"), "rCPU_overload")) return 1;
    if (!getenv("DCAT_PARAM_CORES") || strcmp(getenv("DCAT_PARAM_CORES"), "4")) return 1;
    if (!getenv("DCAT_PARAM_LOSS_PCT") || strcmp(getenv("DCAT_PARAM_LOSS_PCT"), "5")) return 1;

    /* check_tool */
    if (executor_check_tool("/bin/true") != 0) return 1;
    if (executor_check_tool("/nope/nope.sh") == 0) return 1;

    /* mock: executor_run calls mock, returns mock's result */
    executor_set_mock(mock);
    mock_called = 0;
    result_t *r = executor_run(buf);
    if (!mock_called || !r || r->code != 0) return 1;
    if (strcmp(last_cmd, "config/scripts/cpu/cpu_overload.sh")) return 1;
    result_free(r);

    /* mock: executor_run_raw returns 0 */
    int rc = executor_run_raw(buf);
    if (rc != 0) return 1;

    /* real execution (no mock): run /bin/true */
    executor_set_mock(NULL);
    r = executor_run("/bin/true");
    if (!r || r->code != 0) return 1;
    result_free(r);

    /* real execution: run /bin/false → should fail */
    r = executor_run("/bin/false");
    if (!r || r->code == 0) return 1;
    result_free(r);

    /* real run_raw: /bin/true → 0 */
    rc = executor_run_raw("/bin/true");
    if (rc != 0) return 1;

    return 0;
}
