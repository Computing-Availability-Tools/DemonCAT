#include "test.h"
#include "executor.h"
#include "dispatch.h"
#include "registry.h"
#include "state.h"
#include "config.h"
#include "output.h"
#include <string.h>

static const char *g_cmd;
static const char *const *g_env;
static result_t *mock(const char *cmd, const char *const *env) {
    g_cmd = cmd; g_env = env;
    return result_ok("inject", "x", 0, "ok");
}

struct case_t {
    const char *uid; const char *op;
    const char *k1; const char *v1;
    const char *k2; const char *v2;
    const char *expect_cmd_substr; const char *expect_env;
    int expect_state_written;
};
static struct case_t cases[] = {
    {"rCPU_overload", "inject", "cores", "4", NULL, NULL,
        "cpu_overload.sh", "DCAT_PARAM_CORES=4", 1},
    {"rNET_delay", "inject", "iface", "eth0", "delay_ms", "100",
        "net_delay.sh", "DCAT_PARAM_IFACE=eth0", 1},
    {"rPROC_exit", "inject", "pid", "12345", NULL, NULL,
        "proc_exit.sh", "DCAT_PARAM_PID=12345", 0},
};

int test_faults_table(void) {
    config_t cfg; config_load("config/demoncat.conf", &cfg); registry_init(&cfg);
    state_reset(); executor_set_mock(mock);
    for (int i = 0; i < (int)(sizeof(cases)/sizeof(cases[0])); i++) {
        params_t p; params_init(&p);
        params_set(&p, cases[i].k1, cases[i].v1);
        if (cases[i].k2) params_set(&p, cases[i].k2, cases[i].v2);
        result_t *r = dispatch_route(cases[i].uid, cases[i].op, &p);
        ASSERT_TRUE(r != NULL);
        ASSERT_STR_CONTAINS(g_cmd, cases[i].expect_cmd_substr);
        int found = 0;
        for (int e = 0; g_env && g_env[e]; e++)
            if (strcmp(g_env[e], cases[i].expect_env) == 0) found = 1;
        ASSERT_TRUE(found);
        int ids[DCAT_MAX_RECORDS];
        params_t q; params_init(&q);
        int n = state_find_by_params(cases[i].uid, &q, ids, DCAT_MAX_RECORDS);
        if (cases[i].expect_state_written) ASSERT_TRUE(n >= 1);
        else ASSERT_INT_EQ(n, 0);
        result_free(r);
    }
    return 0;
}

int main(void) { RUN_TEST(test_faults_table); return TEST_MAIN_RETURN(); }
