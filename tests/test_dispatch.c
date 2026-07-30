#include "test.h"
#include "dispatch.h"
#include "executor.h"
#include "registry.h"
#include "precheck.h"
#include "state.h"
#include "config.h"
#include "output.h"
#include <string.h>

static int g_called = 0;
static const char *g_last_cmd = NULL;
static result_t *mock_ok(const char *cmd, const char *const *env) {
    (void)env; g_called++; g_last_cmd = cmd;
    return result_ok("inject", "x", 0, "ok");
}

/* 捕获 executor 收到的 DCAT_OP / DCAT_PARAM_CORES，用于验证 clean 的分发 */
static int g_cap_calls = 0;
static const char *g_cap_op = NULL;
static const char *g_cap_cores = NULL;
static result_t *mock_cap(const char *cmd, const char *const *env) {
    (void)cmd;
    g_cap_calls++;
    const char *op = "clean", *cores = NULL;
    for (int i = 0; env[i]; i++) {
        if (strncmp(env[i], "DCAT_OP=", 8) == 0) op = env[i] + 8;
        else if (strncmp(env[i], "DCAT_PARAM_CORES=", 17) == 0) cores = env[i] + 17;
    }
    g_cap_op = op; g_cap_cores = cores;
    return result_ok(op, "rCPU_overload", 0, "cleaned");
}

static config_t cfg;
static void setup(void) {
    config_load("config/demoncat.conf", &cfg);
    registry_init(&cfg);
    state_reset();
    executor_set_mock(mock_ok);
}

int test_dispatch_inject_recoverable_writes_state(void) {
    setup();
    params_t p; params_init(&p);
    params_set(&p, "iface", "eth0");
    params_set(&p, "delay_ms", "100");
    result_t *r = dispatch_route("rNET_delay", "inject", &p);
    ASSERT_TRUE(r != NULL);
    ASSERT_STR_CONTAINS(r->json, "\"status\":\"ok\"");
    ASSERT_TRUE(r->code == 0);
    params_t q; params_init(&q); params_set(&q, "iface", "eth0");
    long long ids[DCAT_MAX_RECORDS]; int n = 0;
    n = state_find_by_params("rNET_delay", &q, ids, DCAT_MAX_RECORDS);
    ASSERT_INT_EQ(n, 1);
    result_free(r); return 0;
}

int test_dispatch_clean_by_params_marks_inactive(void) {
    setup();
    params_t p; params_init(&p);
    params_set(&p, "iface", "eth0");
    params_set(&p, "delay_ms", "100");
    dispatch_route("rNET_delay", "inject", &p);
    ASSERT_INT_EQ(state_list_active(), 1);
    params_t c; params_init(&c); params_set(&c, "iface", "eth0");
    result_t *r = dispatch_route("rNET_delay", "clean", &c);
    ASSERT_TRUE(r != NULL);
    ASSERT_STR_CONTAINS(r->json, "\"status\":\"ok\"");
    ASSERT_INT_EQ(state_list_active(), 0);
    result_free(r); return 0;
}

int test_dispatch_clean_no_match(void) {
    setup();
    params_t c; params_init(&c); params_set(&c, "iface", "eth9");
    result_t *r = dispatch_route("rNET_delay", "clean", &c);
    ASSERT_INT_EQ(r->code, 1);
    result_free(r); return 0;
}

int test_dispatch_list(void) {
    setup();
    result_t *r = dispatch_route(NULL, "list", NULL);
    ASSERT_STR_CONTAINS(r->json, "\"status\":\"ok\"");
    ASSERT_STR_CONTAINS(r->json, "\"op\":\"list\"");
    ASSERT_STR_CONTAINS(r->json, "rNET_delay");
    ASSERT_STR_CONTAINS(r->json, "rPROC_exit");
    result_free(r); return 0;
}

int test_dispatch_query_no_uid_lists_active(void) {
    setup();
    params_t p; params_init(&p);
    params_set(&p, "iface", "eth0");
    params_set(&p, "delay_ms", "100");
    dispatch_route("rNET_delay", "inject", &p);
    result_t *r = dispatch_route(NULL, "query", NULL);
    ASSERT_INT_EQ(r->code, 0);
    ASSERT_STR_CONTAINS(r->json, "rNET_delay");   /* 活跃记录应被列出 */
    ASSERT_STR_CONTAINS(r->json, "record_id");
    result_free(r); return 0;
}

int test_dispatch_uid_not_found(void) {
    setup();
    params_t p; params_init(&p);
    result_t *r = dispatch_route("nope", "inject", &p);
    ASSERT_INT_EQ(r->code, 4);
    result_free(r); return 0;
}
int test_dispatch_query_uid_no_params_ok(void) {
    setup();
    params_t p; params_init(&p);
    /* query 带 uid 但无参：按 SPEC 不应被拒（脚本自行展示全部），不再返回 code 3 */
    result_t *r = dispatch_route("rNET_delay", "query", &p);
    ASSERT_INT_EQ(r->code, 0);
    result_free(r);
    return 0;
}

/* clean <uid> 无参 = clean-all-for-uid：直接调脚本 clean（不传 DCAT_PARAM_*），绕过 state */
int test_dispatch_clean_no_params_invokes_script(void) {
    setup();
    executor_set_mock(mock_cap);
    g_cap_calls = 0; g_cap_op = NULL; g_cap_cores = "SENTINEL";
    params_t empty; params_init(&empty);
    result_t *r = dispatch_route("rCPU_overload", "clean", &empty);
    ASSERT_TRUE(r != NULL);
    ASSERT_INT_EQ(r->code, 0);
    ASSERT_INT_EQ(g_cap_calls, 1);
    ASSERT_STREQ(g_cap_op, "clean");
    ASSERT_TRUE(g_cap_cores == NULL);   /* 未传 DCAT_PARAM_CORES */
    result_free(r);
    return 0;
}

/* clean --all：对每个支持 clean 的注册故障各调一次无参 clean，聚合返回 ok */
int test_dispatch_clean_all_fans_out(void) {
    setup();
    executor_set_mock(mock_cap);
    g_cap_calls = 0; g_cap_op = NULL;
    result_t *r = dispatch_clean_all();
    ASSERT_TRUE(r != NULL);
    ASSERT_INT_EQ(r->code, 0);
    int nf = 0; const fault_def_t *lst = registry_list(&nf);
    int expect = 0;
    for (int i = 0; i < nf; i++) if (op_in_supported(lst[i].supported_ops, "clean")) expect++;
    ASSERT_INT_EQ(g_cap_calls, expect);
    ASSERT_STREQ(g_cap_op, "clean");
    result_free(r);
    return 0;
}

/* state 丢失(文件缺失)下 clean <uid> --params 应回退用用户参数调脚本 clean */
int test_dispatch_clean_state_lost_fallback(void) {
    setup();
    executor_set_mock(mock_cap);
    state_set_file("/tmp/dcat-test-state-missing2.json");
    state_load();                       /* 文件缺失 → state_is_lost()=1，内存空 */
    g_cap_calls = 0; g_cap_op = NULL; g_cap_cores = "SENTINEL";
    params_t p; params_init(&p); params_set(&p, "cores", "0");
    result_t *r = dispatch_route("rCPU_overload", "clean", &p);
    ASSERT_TRUE(r != NULL);
    ASSERT_INT_EQ(r->code, 0);
    ASSERT_INT_EQ(g_cap_calls, 1);
    ASSERT_STREQ(g_cap_op, "clean");
    ASSERT_STREQ(g_cap_cores, "0");
    result_free(r);
    return 0;
}

/* clean <uid> 无参（stateless）：脚本清 /tmp 工件后，必须 reconcile state——
 * 把该 uid 全部活跃记录标 inactive，避免 query 残留幽灵记录。 */
int test_dispatch_clean_no_params_marks_state_inactive(void) {
    setup();
    params_t p; params_init(&p); params_set(&p, "cores", "0");
    dispatch_route("rCPU_overload", "inject", &p);
    ASSERT_INT_EQ(state_list_active(), 1);
    params_t empty; params_init(&empty);
    result_t *r = dispatch_route("rCPU_overload", "clean", &empty);
    ASSERT_TRUE(r != NULL);
    ASSERT_INT_EQ(r->code, 0);
    ASSERT_INT_EQ(state_list_active(), 0);   /* reconcile 后无幽灵记录 */
    result_free(r); return 0;
}

/* clean --all：fan-out 无参 clean 后，全部活跃记录应被 reconcile 为 inactive。 */
int test_dispatch_clean_all_marks_state_inactive(void) {
    setup();
    params_t a; params_init(&a); params_set(&a, "cores", "0");
    dispatch_route("rCPU_overload", "inject", &a);
    params_t b; params_init(&b); params_set(&b, "iface", "eth0"); params_set(&b, "delay_ms", "100");
    dispatch_route("rNET_delay", "inject", &b);
    ASSERT_INT_EQ(state_list_active(), 2);
    result_t *r = dispatch_clean_all();
    ASSERT_TRUE(r != NULL);
    ASSERT_INT_EQ(r->code, 0);
    ASSERT_INT_EQ(state_list_active(), 0);
    result_free(r); return 0;
}

int main(void) {
    RUN_TEST(test_dispatch_inject_recoverable_writes_state);
    RUN_TEST(test_dispatch_clean_by_params_marks_inactive);
    RUN_TEST(test_dispatch_clean_no_match);
    RUN_TEST(test_dispatch_list);
    RUN_TEST(test_dispatch_query_no_uid_lists_active);
    RUN_TEST(test_dispatch_query_uid_no_params_ok);
    RUN_TEST(test_dispatch_uid_not_found);
    RUN_TEST(test_dispatch_clean_no_params_invokes_script);
    RUN_TEST(test_dispatch_clean_no_params_marks_state_inactive);
    RUN_TEST(test_dispatch_clean_all_fans_out);
    RUN_TEST(test_dispatch_clean_all_marks_state_inactive);
    RUN_TEST(test_dispatch_clean_state_lost_fallback);
    return TEST_MAIN_RETURN();
}
