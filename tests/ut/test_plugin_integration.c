#include "test.h"
#include "plugins/plugin_manager.h"
#include "dispatch.h"
#include "state.h"
#include "output.h"
#include <string.h>

int test_plugin_load_and_dispatch(void) {
    state_reset();
    int n = plugin_load_dir("plugins");
    ASSERT_TRUE(n >= 1);
    ASSERT_TRUE(plugin_find("rSAMPLE_test") != NULL);

    /* inject：三级回退命中插件，写 state，有 record_id */
    params_t p;
    params_init(&p);
    result_t *r = dispatch_route("rSAMPLE_test", "inject", &p);
    ASSERT_TRUE(r != NULL);
    ASSERT_STR_CONTAINS(r->json, "\"status\":\"ok\"");
    ASSERT_STR_CONTAINS(r->json, "rSAMPLE_test");
    ASSERT_STR_CONTAINS(r->json, "\"record_id\"");
    ASSERT_TRUE(r->code == 0);
    ASSERT_TRUE(state_list_active() == 1);
    result_free(r);

    /* clean：按参数匹配，mark inactive */
    params_t c;
    params_init(&c);
    result_t *rc = dispatch_route("rSAMPLE_test", "clean", &c);
    ASSERT_TRUE(rc != NULL);
    ASSERT_STR_CONTAINS(rc->json, "\"status\":\"ok\"");
    ASSERT_TRUE(state_list_active() == 0);
    result_free(rc);
    return 0;
}

int test_plugin_query_route(void) {
    if (!plugin_find("rSAMPLE_test")) plugin_load_dir("plugins");
    state_reset();
    params_t p;
    params_init(&p);
    result_t *r = dispatch_route("rSAMPLE_test", "query", &p);
    ASSERT_TRUE(r != NULL);
    ASSERT_INT_EQ(r->code, 0);
    ASSERT_STR_CONTAINS(r->json, "sample confirmed");
    result_free(r);
    return 0;
}

int test_plugin_op_not_supported(void) {
    if (!plugin_find("rSAMPLE_test")) plugin_load_dir("plugins");
    state_reset();
    params_t p;
    params_init(&p);
    result_t *r = dispatch_route("rSAMPLE_test", "bogop", &p);
    ASSERT_TRUE(r != NULL);
    ASSERT_INT_EQ(r->code, 3);
    ASSERT_STR_CONTAINS(r->json, "op not in supported_ops");
    result_free(r);
    return 0;
}

int test_plugin_undeclared_param(void) {
    if (!plugin_find("rSAMPLE_test")) plugin_load_dir("plugins");
    state_reset();
    params_t p;
    params_init(&p);
    params_set(&p, "bogus_key", "val");
    result_t *r = dispatch_route("rSAMPLE_test", "inject", &p);
    ASSERT_TRUE(r != NULL);
    ASSERT_INT_EQ(r->code, 3);
    ASSERT_STR_CONTAINS(r->json, "unknown parameter");
    ASSERT_STR_CONTAINS(r->json, "bogus_key");
    result_free(r);
    return 0;
}

int test_plugin_clean_no_active(void) {
    if (!plugin_find("rSAMPLE_test")) plugin_load_dir("plugins");
    state_reset();
    params_t p;
    params_init(&p);
    result_t *r = dispatch_route("rSAMPLE_test", "clean", &p);
    ASSERT_TRUE(r != NULL);
    ASSERT_INT_EQ(r->code, 0); /* idempotent: already clean */
    ASSERT_STR_CONTAINS(r->json, "already clean");
    result_free(r);
    return 0;
}

int test_plugin_state_full(void) {
    if (!plugin_find("rSAMPLE_test")) plugin_load_dir("plugins");
    state_reset();
    params_t p;
    params_init(&p);
    for (int i = 0; i < DCAT_MAX_RECORDS; i++) {
        result_t *r = dispatch_route("rSAMPLE_test", "inject", &p);
        ASSERT_TRUE(r != NULL);
        ASSERT_INT_EQ(r->code, 0);
        result_free(r);
    }
    ASSERT_INT_EQ(state_list_active(), DCAT_MAX_RECORDS);
    result_t *r = dispatch_route("rSAMPLE_test", "inject", &p);
    ASSERT_TRUE(r != NULL);
    ASSERT_INT_EQ(r->code, 1);
    ASSERT_STR_CONTAINS(r->json, "state table full");
    result_free(r);
    state_reset();
    return 0;
}

int test_plugin_duplicate_uid_rejected(void) {
    if (!plugin_find("rSAMPLE_test")) plugin_load_dir("plugins");
    int before = plugin_count();
    int n = plugin_load_dir("plugins");
    ASSERT_INT_EQ(n, 0);
    ASSERT_INT_EQ(plugin_count(), before);
    return 0;
}

int main(void) {
    RUN_TEST(test_plugin_load_and_dispatch);
    RUN_TEST(test_plugin_query_route);
    RUN_TEST(test_plugin_op_not_supported);
    RUN_TEST(test_plugin_undeclared_param);
    RUN_TEST(test_plugin_clean_no_active);
    RUN_TEST(test_plugin_state_full);
    RUN_TEST(test_plugin_duplicate_uid_rejected);
    return TEST_MAIN_RETURN();
}
