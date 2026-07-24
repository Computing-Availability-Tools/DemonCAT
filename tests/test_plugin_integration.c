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
    params_t p; params_init(&p);
    result_t *r = dispatch_route("rSAMPLE_test", "inject", &p);
    ASSERT_TRUE(r != NULL);
    ASSERT_STR_CONTAINS(r->json, "\"status\":\"ok\"");
    ASSERT_STR_CONTAINS(r->json, "rSAMPLE_test");
    ASSERT_STR_CONTAINS(r->json, "\"record_id\"");
    ASSERT_TRUE(r->code == 0);
    ASSERT_TRUE(state_list_active() == 1);
    result_free(r);

    /* clean：按参数匹配，mark inactive */
    params_t c; params_init(&c);
    result_t *rc = dispatch_route("rSAMPLE_test", "clean", &c);
    ASSERT_TRUE(rc != NULL);
    ASSERT_STR_CONTAINS(rc->json, "\"status\":\"ok\"");
    ASSERT_TRUE(state_list_active() == 0);
    result_free(rc);
    return 0;
}

int main(void) {
    RUN_TEST(test_plugin_load_and_dispatch);
    return TEST_MAIN_RETURN();
}
