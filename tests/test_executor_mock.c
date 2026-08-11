#include "test.h"
#include "executor.h"
#include "types.h"
#include "output.h"
#include <string.h>

static const char *g_last_cmd = NULL;
static const char *const *g_last_env = NULL;
static result_t *my_mock(const char *cmd, const char *const *env) {
    g_last_cmd = cmd;
    g_last_env = env;
    return result_ok("inject", "rNET_loss", 0, "mocked");
}

int test_build_cmd_and_env(void) {
    executor_set_mock(my_mock);
    fault_def_t f;
    memset(&f, 0, sizeof(f));
    strcpy(f.uid, "rNET_loss");
    strcpy(f.script, "/x/net_loss.sh");
    params_t p;
    params_init(&p);
    params_set(&p, "iface", "eth0");
    params_set(&p, "loss_pct", "5");
    result_t *r = executor_run_fault(&f, "inject", &p, 0); /* timeout=0 不超时 */
    ASSERT_TRUE(g_last_cmd != NULL);
    ASSERT_STR_CONTAINS(g_last_cmd, "/x/net_loss.sh");
    ASSERT_TRUE(g_last_env != NULL);
    int found_op = 0, found_uid = 0, found_iface = 0, found_loss = 0;
    for (int i = 0; g_last_env && g_last_env[i]; i++) {
        if (strcmp(g_last_env[i], "DCAT_OP=inject") == 0) found_op = 1;
        if (strcmp(g_last_env[i], "DCAT_UID=rNET_loss") == 0) found_uid = 1;
        if (strcmp(g_last_env[i], "DCAT_PARAM_IFACE=eth0") == 0) found_iface = 1;
        if (strcmp(g_last_env[i], "DCAT_PARAM_LOSS_PCT=5") == 0) found_loss = 1;
    }
    ASSERT_TRUE(found_op && found_uid && found_iface && found_loss);
    result_free(r);
    return 0;
}

int test_run_raw_mock(void) {
    executor_set_mock(my_mock);
    fault_def_t f;
    memset(&f, 0, sizeof(f));
    strcpy(f.uid, "rCPU_overload");
    strcpy(f.script, "/x/cpu_overload.sh");
    params_t p;
    params_init(&p);
    params_set(&p, "cores", "2");
    int rc = executor_run_raw_fault(&f, "query", &p);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_TRUE(g_last_cmd != NULL);
    ASSERT_STR_CONTAINS(g_last_cmd, "/x/cpu_overload.sh");
    int found_op = 0;
    for (int i = 0; g_last_env && g_last_env[i]; i++)
        if (strcmp(g_last_env[i], "DCAT_OP=query") == 0) found_op = 1;
    ASSERT_TRUE(found_op);
    return 0;
}

int main(void) {
    RUN_TEST(test_build_cmd_and_env);
    RUN_TEST(test_run_raw_mock);
    return TEST_MAIN_RETURN();
}
