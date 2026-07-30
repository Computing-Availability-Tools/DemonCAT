#include "test.h"
#include "dispatch.h"
#include "executor.h"
#include "registry.h"
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
    return TEST_MAIN_RETURN();
}
