#include "test.h"
#include "plugins/plugin_manager.h"
#include "plugins/plugin.h"

int test_plugin_find_empty(void) {
    ASSERT_TRUE(plugin_find("nope") == NULL);
    ASSERT_INT_EQ(plugin_count(), 0);
    return 0;
}

int test_plugin_load_empty_dir(void) {
    int n = plugin_load_dir("/tmp/dcat-no-plugins-here-xyz");
    ASSERT_INT_EQ(n, 0);
    ASSERT_TRUE(plugin_find("nope") == NULL);
    ASSERT_INT_EQ(plugin_count(), 0);
    return 0;
}

int test_plugin_list_empty_and_fini_noop(void) {
    int n = -1;
    const dcat_plugin_t *const *list = plugin_list(&n);
    ASSERT_TRUE(list != NULL);
    ASSERT_INT_EQ(n, 0);
    ASSERT_INT_EQ(plugin_count(), 0);
    plugin_fini();
    ASSERT_INT_EQ(plugin_count(), 0);
    ASSERT_TRUE(plugin_find("nope") == NULL);
    return 0;
}

int main(void) {
    RUN_TEST(test_plugin_find_empty);
    RUN_TEST(test_plugin_load_empty_dir);
    RUN_TEST(test_plugin_list_empty_and_fini_noop);
    return TEST_MAIN_RETURN();
}
